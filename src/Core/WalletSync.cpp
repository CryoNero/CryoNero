// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include "WalletSync.hpp"
#include "Config.hpp"
#include "CryptoNoteTools.hpp"
#include "TransactionBuilder.hpp"
#include "platform/Time.hpp"
#include "seria/BinaryInputStream.hpp"
#include "seria/BinaryOutputStream.hpp"
#include "seria/KVBinaryInputStream.hpp"
#include "seria/KVBinaryOutputStream.hpp"

constexpr float STATUS_POLL_PERIOD = 0.1f;
constexpr float STATUS_ERROR_PERIOD = 5;

using namespace cryonerocoin;

WalletSync::WalletSync(
	logging::ILogger &log, const Config &config, WalletState &wallet_state, std::function<void()> state_changed_handler)
	: m_state_changed_handler(state_changed_handler)
	, m_log(log, "WalletSync")
	, m_config(config)
	, m_sync_error("CONNECTING")
	, m_status_timer(std::bind(&WalletSync::send_get_status, this))
	, m_sync_agent(config.cryonerod_remote_ip,
		config.cryonerod_remote_port ? config.cryonerod_remote_port : config.cryonerod_bind_port)
	, m_commands_agent(config.cryonerod_remote_ip,
		config.cryonerod_remote_port ? config.cryonerod_remote_port : config.cryonerod_bind_port)
	, m_wallet_state(wallet_state)
	, m_commit_timer(std::bind(&WalletSync::db_commit, this)) {
	advance_sync();
	m_commit_timer.once(DB_COMMIT_PERIOD_WALLET_CACHE);
}

void WalletSync::db_commit() {
	m_wallet_state.db_commit();
	m_commit_timer.once(DB_COMMIT_PERIOD_WALLET_CACHE);
}

void WalletSync::send_get_status() {
	api::cryonerod::GetStatus::Request req;
	req.top_block_hash = m_wallet_state.get_tip_bid();
	req.transaction_pool_version = m_wallet_state.get_tx_pool_version();
	req.outgoing_peer_count = m_last_node_status.outgoing_peer_count;
	req.incoming_peer_count = m_last_node_status.incoming_peer_count;
	req.lower_level_error = m_last_node_status.lower_level_error;
	json_rpc::Request json_send_raw_req;
	json_send_raw_req.set_method(api::cryonerod::GetStatus::method());
	json_send_raw_req.set_params(req);
	http::RequestData req_header;
	req_header.r.set_firstline("POST", api::cryonerod::url(), 1, 1);
	req_header.r.basic_authorization = m_config.cryonerod_authorization;
	req_header.set_body(json_send_raw_req.get_body());

	m_sync_request = std::make_unique<http::Request>(m_sync_agent, std::move(req_header),
		[&](http::ResponseData &&response) {
		m_sync_request.reset();
		if (response.r.status == 504) {  // Common for longpoll
			advance_sync();
		}
		else if (response.r.status == 401) {
			m_sync_error = "AUTHORIZATION_FAILED";
			m_log(logging::INFO) << "Wrong daemon password - please check --cryonerod-authorization" << std::endl;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		else {
			api::cryonerod::GetStatus::Response resp;
			json_rpc::parse_response(response.body, resp);
			m_last_node_status = resp;
			m_sync_error = std::string();
			m_state_changed_handler();
			advance_sync();
		}
	},
		[&](std::string err) {
		m_sync_error = "CONNECTION_FAILED";
		m_status_timer.once(STATUS_ERROR_PERIOD);
		m_state_changed_handler();
	});
}

void WalletSync::advance_sync() {
	const Timestamp now = platform::now_unix_timestamp();
	if (!prevent_sleep && m_wallet_state.get_tip().timestamp < now - 86400) {
		m_log(logging::INFO) << "Preventing computer sleep to sync wallet" << std::endl;
		prevent_sleep = std::make_unique<platform::PreventSleep>("Synchronizing wallet");
	}
	if (prevent_sleep &&
		m_wallet_state.get_tip().timestamp > now - m_wallet_state.get_currency().get_block_future_time_limit(m_wallet_state.get_tip_height() + 1) * 2)
	{
		m_log(logging::INFO) << "Allowing computer sleep after sync wallet" << std::endl;
		prevent_sleep = nullptr;
	}
	if (m_sync_request)
		return;
	if (m_last_node_status.top_block_hash != m_wallet_state.get_tip_bid()) {
		next_send_hash = Hash{};  
		send_get_blocks();
		return;
	}
	if (send_send_transaction())
		return;
	if (m_last_node_status.transaction_pool_version == m_wallet_state.get_tx_pool_version()) {
		m_status_timer.once(STATUS_POLL_PERIOD);
		return;
	}
	send_sync_pool();
}

void WalletSync::send_sync_pool() {
	m_log(logging::TRACE) << "Sending SyncMemPool request" << std::endl;
	api::cryonerod::SyncMemPool::Request msg;
	msg.known_hashes = m_wallet_state.get_tx_pool_hashes();
	http::RequestData req_header;
	req_header.r.set_firstline("POST", api::cryonerod::SyncMemPool::bin_method(), 1, 1);
	req_header.r.basic_authorization = m_config.cryonerod_authorization;
	req_header.set_body(seria::to_binary_str(msg));
	m_sync_request = std::make_unique<http::Request>(m_sync_agent, std::move(req_header),
		[&](http::ResponseData &&response) {
		m_sync_request.reset();
		m_log(logging::TRACE) << "Received SyncMemPool response status=" << response.r.status << std::endl;
		if (response.r.status == 401) {
			m_sync_error = "AUTHORIZATION_FAILED";
			m_log(logging::INFO) << "Wrong daemon password - please check --cryonerod-authorization" << std::endl;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		else if (response.r.status == 410) {
			m_sync_error = "WRONG_DAEMON_VERSION";
			m_log(logging::INFO) << "Wrong daemon version - please upgrade cryonerod" << std::endl;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		else if (response.r.status == 200) {
			m_sync_error = "WRONG_BLOCKCHAIN";
			api::cryonerod::SyncMemPool::Response resp;
			seria::from_binary(resp, response.body);
			m_last_node_status = resp.status;
			if (m_wallet_state.sync_with_blockchain(resp)) {
				m_sync_error = std::string();
				advance_sync();
			}
			else
				m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		else {
			m_sync_error = response.body;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		m_state_changed_handler();
	},
		[&](std::string err) {
		m_log(logging::TRACE) << "SyncMemPool request error " << err << std::endl;
		m_sync_error = "CONNECTION_FAILED";
		m_status_timer.once(STATUS_ERROR_PERIOD);
		m_state_changed_handler();
	});
}

void WalletSync::send_get_blocks() {
	m_log(logging::TRACE) << "Sending SyncBlocks request" << std::endl;
	api::cryonerod::SyncBlocks::Request msg;
	msg.sparse_chain = m_wallet_state.get_sparse_chain();
	msg.first_block_timestamp = m_wallet_state.get_wallet().get_oldest_timestamp();
	http::RequestData req_header;
	req_header.r.set_firstline("POST", api::cryonerod::SyncBlocks::bin_method(), 1, 1);
	req_header.r.basic_authorization = m_config.cryonerod_authorization;
	req_header.set_body(seria::to_binary_str(msg));
	m_sync_request = std::make_unique<http::Request>(m_sync_agent, std::move(req_header),
		[&](http::ResponseData &&response) {
		m_sync_request.reset();
		m_log(logging::TRACE) << "Received SyncBlocks response status=" << response.r.status << std::endl;
		if (response.r.status == 401) {
			m_sync_error = "AUTHORIZATION_FAILED";
			m_log(logging::INFO) << "Wrong daemon password - please check --cryonerod-authorization" << std::endl;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		else if (response.r.status == 410) {
			m_sync_error = "WRONG_DAEMON_VERSION";
			m_log(logging::INFO) << "Wrong daemon version - please upgrade cryonerod" << std::endl;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		else if (response.r.status == 200) {
			m_sync_error = "WRONG_BLOCKCHAIN";
			api::cryonerod::SyncBlocks::Response resp;
			seria::from_binary(resp, response.body);
			m_last_node_status = resp.status;
			if (m_wallet_state.sync_with_blockchain(resp)) {
				m_sync_error = std::string();
				advance_sync();
			}
			else
				m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		else {
			m_sync_error = response.body;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		m_state_changed_handler();
	},
		[&](std::string err) {
		m_log(logging::TRACE) << "SyncBlocks request error " << err << std::endl;
		m_sync_error = "CONNECTION_FAILED";
		m_status_timer.once(STATUS_ERROR_PERIOD);
		m_state_changed_handler();
	});
}

bool WalletSync::send_send_transaction() 
{
	api::cryonerod::SendTransaction::Request msg;
	msg.binary_transaction = m_wallet_state.get_next_from_sending_queue(&next_send_hash);
	if (msg.binary_transaction.empty())
		return false;
	sending_transaction_hash = next_send_hash;
	m_log(logging::INFO) << "Sending transaction from payment queue " << sending_transaction_hash << std::endl;
	auto new_request = json_rpc::create_request(api::cryonerod::url(), api::cryonerod::SendTransaction::method(), msg);
	new_request.r.basic_authorization = m_config.cryonerod_authorization;
	m_sync_request = std::make_unique<http::Request>(m_sync_agent, std::move(new_request),
		[&](http::ResponseData &&response) {
		m_sync_request.reset();
		m_log(logging::TRACE) << "Received send_transaction response status=" << response.r.status << std::endl;
		if (response.r.status == 401) {
			m_sync_error = "AUTHORIZATION_FAILED";
			m_log(logging::INFO) << "Wrong daemon password - please check --cryonerod-authorization" << std::endl;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		else if (response.r.status == 200) {
			m_sync_error = "SEND_ERROR";
			json_rpc::Response json_resp(response.body);
			api::cryonerod::SendTransaction::Response resp;
			api::cryonerod::SendTransaction::Error err_resp;
			if (json_resp.get_error(err_resp)) {
				m_log(logging::INFO) << "Json Error sending transaction from payment queue conflict height="
					<< err_resp.conflict_height << " code=" << err_resp.code
					<< " msg=" << err_resp.message << std::endl;
				m_wallet_state.process_payment_queue_send_error(sending_transaction_hash, err_resp);
			}
			else {
				json_resp.get_result(resp);
				m_log(logging::INFO) << "Success sending transaction from payment queue with result "
					<< resp.send_result << std::endl;
				m_sync_error = std::string();
			}
			advance_sync();
		}
		else {
			m_log(logging::INFO) << "Error sending transaction from payment queue " << response.body << std::endl;
			m_sync_error = response.body;
			m_status_timer.once(STATUS_ERROR_PERIOD);
		}
		m_state_changed_handler();
	},
		[&](std::string err) {
		m_log(logging::INFO) << "Error sending transaction from payment queue " << err << std::endl;
		m_status_timer.once(STATUS_ERROR_PERIOD);
		m_state_changed_handler();
	});
	return true;
}
