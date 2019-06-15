// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The CryoNero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#pragma once

#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include "BlockChainFileFormat.hpp"
#include "BlockChainState.hpp"
#include "http/JsonRpc.hpp"
#include "http/Server.hpp"
#include "p2p/P2P.hpp"
#include "p2p/P2PClientBasic.hpp"
#include "platform/PreventSleep.hpp"
#include "rpc_api.hpp"
#include "node_api_extensions.hpp"

namespace cryonero {

static const float DB_COMMIT_PERIOD_WALLET_CACHE = 290;  // 5 minutes sounds good compromise
static const float DB_COMMIT_PERIOD_cryonerod    = 310;  // 5 minutes sounds good compromise
static const float SYNC_TIMEOUT                  = 20;   // If sync does not return, select different sync node after
static const int DOWNLOAD_CONCURRENCY            = 4;
static const int DOWNLOAD_QUEUE                  = 10;  // number of block requests sent before receiving reply
static const int DOWNLOAD_BLOCK_WINDOW           = DOWNLOAD_CONCURRENCY * DOWNLOAD_QUEUE * 2;
static const float RETRY_DOWNLOAD_SECONDS        = 10;

class Node {
public:
	using HTTPHandlerFunction = std::function<bool(Node *, http::Client *, http::RequestData &&, http::ResponseData &)>;
	using JSONRPCHandlerFunction = std::function<bool(Node *, http::Client *, http::RequestData &&, json_rpc::Request &&, json_rpc::Response &)>;

	explicit Node(logging::ILogger &, const Config &, BlockChainState &);
	bool on_idle();

	// binary method
	bool on_wallet_sync3(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::SyncBlocks::Request &&, api::cryonerod::SyncBlocks::Response &);
	bool on_sync_mempool3(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::SyncMemPool::Request &&, api::cryonerod::SyncMemPool::Response &);
	bool on_get_raw_transaction3(http::Client *, http::RequestData &&, json_rpc::Request &&,
	   // api::cryonerod::GetRawTransaction::Request &&, api::cryonerod::GetRawTransaction::Response &);
		api::cryonerod::GetRawTransaction::Request &&, api::cryonerod::GetRawTransaction::Response &);
	bool on_get_raw_block(http::Client *, http::RequestData &&, json_rpc::Request &&,
		api::cryonerod::GetRawBlock::Request &&, api::cryonerod::GetRawBlock::Response &);



	api::cryonerod::GetStatus::Response create_status_response3() const;
	// json_rpc_node
	bool on_get_status3(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::GetStatus::Request &&, api::cryonerod::GetStatus::Response &);
	bool on_get_random_outputs3(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::GetRandomOutputs::Request &&, api::cryonerod::GetRandomOutputs::Response &);
	bool handle_send_transaction3(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::SendTransaction::Request &&, api::cryonerod::SendTransaction::Response &);
	bool handle_check_sendproof3(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::CheckSendProof::Request &&, api::cryonerod::CheckSendProof::Response &);
	bool on_getblocktemplate(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::GetBlockTemplate::Request &&r, api::cryonerod::GetBlockTemplate::Response &);
	void getblocktemplate(
	    const api::cryonerod::GetBlockTemplate::Request &, api::cryonerod::GetBlockTemplate::Response &);
	bool on_get_currency_id(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::GetCurrencyId::Request &&, api::cryonerod::GetCurrencyId::Response &);
	bool on_submitblock(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::SubmitBlock::Request &&, api::cryonerod::SubmitBlock::Response &);
	bool on_submitblock_legacy(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::SubmitBlockLegacy::Request &&, api::cryonerod::SubmitBlockLegacy::Response &);
	bool on_get_last_block_header(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::GetLastBlockHeaderLegacy::Request &&, api::cryonerod::GetLastBlockHeaderLegacy::Response &);
	bool on_get_block_header_by_hash(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::GetBlockHeaderByHashLegacy::Request &&, api::cryonerod::GetBlockHeaderByHashLegacy::Response &);
	bool on_get_block_header_by_height(http::Client *, http::RequestData &&, json_rpc::Request &&,
	    api::cryonerod::GetBlockHeaderByHeightLegacy::Request &&,
	    api::cryonerod::GetBlockHeaderByHeightLegacy::Response &);

	// Node API Extensions
	bool on_get_blocks_json(http::Client *, http::RequestData &&, json_rpc::Request &&,
		api::extensions::GetBlocks::Request &&, api::extensions::GetBlocks::Response &);
	bool on_get_block_json(http::Client *, http::RequestData &&, json_rpc::Request &&,
		api::extensions::GetBlock::Request &&, api::extensions::GetBlock::Response &);
	bool on_get_transaction_json(http::Client *, http::RequestData &&, json_rpc::Request &&,
		api::extensions::GetTransaction::Request &&, api::extensions::GetTransaction::Response &);
	bool on_get_mempool_json(http::Client *, http::RequestData &&, json_rpc::Request &&,
		api::extensions::GetMempool::Request &&, api::extensions::GetMempool::Response &);

	bool process_json_rpc_request(http::Client *, http::RequestData &&, http::ResponseData &);

	BlockChainState &m_block_chain;
	const Config &m_config;

protected:
	// We read from both because any could be truncated/corrupted
	std::unique_ptr<LegacyBlockChainReader> m_block_chain_reader1;
	std::unique_ptr<LegacyBlockChainReader> m_block_chain_reader2;
	std::unique_ptr<http::Server> m_api;
	std::unique_ptr<platform::PreventSleep> m_prevent_sleep;
	struct LongPollClient {
		http::Client *original_who = nullptr;
		http::RequestData original_request;
		json_rpc::Request original_json_request;
		api::cryonerod::GetStatus::Request original_get_status;
	};
	std::list<LongPollClient> m_long_poll_http_clients;
	void advance_long_poll();

	bool m_block_chain_was_far_behind;
	logging::LoggerRef m_log;
	PeerDB m_peer_db;
	P2P m_p2p;
	const Timestamp m_start_time;
	platform::Timer m_commit_timer;
	std::unique_ptr<platform::PreventSleep> prevent_sleep;
	void db_commit() {
		m_block_chain.db_commit();
		m_commit_timer.once(DB_COMMIT_PERIOD_cryonerod);
	}

	bool check_trust(const proof_of_trust &);
	uint64_t m_last_stat_request_time = 0;
	// Prevent replay attacks by only trusting requests with timestamp > than previous request

	class P2PClientcryonero : public P2PClientBasic {
		Node *const m_node;
		void after_handshake();

	protected:
		virtual void on_disconnect(const std::string &ban_reason) override;

		virtual void on_msg_bytes(size_t downloaded, size_t uploaded) override;
		virtual CORE_SYNC_DATA get_sync_data() const override;
		virtual std::vector<PeerlistEntry> get_peers_to_share() const override;

		virtual void on_first_message_after_handshake() override;
		virtual void on_msg_handshake(COMMAND_HANDSHAKE::request &&) override;
		virtual void on_msg_handshake(COMMAND_HANDSHAKE::response &&) override;
		virtual void on_msg_notify_request_chain(NOTIFY_REQUEST_CHAIN::request &&) override;
		virtual void on_msg_notify_request_chain(NOTIFY_RESPONSE_CHAIN_ENTRY::request &&) override;
		virtual void on_msg_notify_request_objects(NOTIFY_REQUEST_GET_OBJECTS::request &&) override;
		virtual void on_msg_notify_request_objects(NOTIFY_RESPONSE_GET_OBJECTS::request &&) override;
		virtual void on_msg_notify_request_tx_pool(NOTIFY_REQUEST_TX_POOL::request &&) override;
		virtual void on_msg_timed_sync(COMMAND_TIMED_SYNC::request &&) override;
		virtual void on_msg_timed_sync(COMMAND_TIMED_SYNC::response &&) override;
		virtual void on_msg_notify_new_block(NOTIFY_NEW_BLOCK::request &&) override;
		virtual void on_msg_notify_new_transactions(NOTIFY_NEW_TRANSACTIONS::request &&) override;
#if cryonero_ALLOW_DEBUG_COMMANDS
		virtual void on_msg_network_state(COMMAND_REQUEST_NETWORK_STATE::request &&) override;
		virtual void on_msg_stat_info(COMMAND_REQUEST_STAT_INFO::request &&) override;
#endif
	public:
		explicit P2PClientcryonero(Node *node, bool incoming, D_handler d_handler)
		    : P2PClientBasic(node->m_config, node->m_p2p.get_unique_number(), incoming, d_handler), m_node(node) {}
		Node *get_node() const { return m_node; }
	};
	std::unique_ptr<P2PClient> client_factory(bool incoming, P2PClient::D_handler d_handler) {
		return std::make_unique<P2PClientcryonero>(this, incoming, d_handler);
	}

	class DownloaderV11 {  // torrent-style sync&download from legacy v1 clients
		Node *const m_node;
		BlockChainState &m_block_chain;

		std::map<P2PClientcryonero *, size_t> m_good_clients;  // -> # of downloading blocks
		size_t total_downloading_blocks = 0;
		std::list<P2PClientcryonero *> m_who_downloaded_block;
		P2PClientcryonero *m_chain_client = nullptr;
		bool m_chain_request_sent         = false;
		platform::Timer m_chain_timer;  // If m_chain_client does not respond for long, disconnect it

		struct DownloadCell {
			Hash bid;
			Height expected_height = 0;
			NetworkAddress bid_source;  // for banning culprit in case of a problem
			NetworkAddress block_source;  // for banning culprit in case of a problem
			P2PClientcryonero *downloading_client = nullptr;
			std::chrono::steady_clock::time_point request_time;
			RawBlock rb;
			enum Status { DOWNLOADING, DOWNLOADED, PREPARING, PREPARED } status = DOWNLOADING;
			bool protect_from_disconnect = false;
			PreparedBlock pb;
		};
		std::deque<DownloadCell>
		    m_download_chain;  // ~20-1000 of blocks we wish to have downloading (depending on current median size)
		                       //		Height m_protected_start = 0;
		Height m_chain_start_height = 0;
		std::deque<Hash> m_chain;     // 10k-20k of hashes of the next wanted blocks
		NetworkAddress chain_source;  // for banning culprit in case of a problem
		platform::Timer m_download_timer;
		std::chrono::steady_clock::time_point log_request_timestamp;
		std::chrono::steady_clock::time_point log_response_timestamp;

		// multicore preparator
		std::vector<std::thread> threads;
		std::mutex mu;
		std::map<Hash, PreparedBlock> prepared_blocks;
		std::deque<std::tuple<Hash, bool, RawBlock>> work;
		std::condition_variable have_work;
		platform::EventLoop *main_loop = nullptr;
		bool quit                      = false;
		void add_work(std::tuple<Hash, bool, RawBlock> &&wo);
		void thread_run();

		void start_download(DownloadCell &dc, P2PClientcryonero *who);
		void stop_download(DownloadCell &dc, bool success);
		void on_chain_timer();
		void on_download_timer();
		void advance_chain();

	public:
		DownloaderV11(Node *node, BlockChainState &block_chain);
		~DownloaderV11();

		void advance_download();
		bool on_idle();

		uint32_t get_known_block_count(uint32_t my) const;
		void on_connect(P2PClientcryonero *);
		void on_disconnect(P2PClientcryonero *);
		const std::map<P2PClientcryonero *, size_t> &get_good_clients() const { return m_good_clients; }
		void on_msg_notify_request_chain(P2PClientcryonero *, const NOTIFY_RESPONSE_CHAIN_ENTRY::request &);
		void on_msg_notify_request_objects(P2PClientcryonero *, const NOTIFY_RESPONSE_GET_OBJECTS::request &);
	};

	DownloaderV11 m_downloader;

	bool on_api_http_request(http::Client *, http::RequestData &&, http::ResponseData &);
	void on_api_http_disconnect(http::Client *);

	void sync_transactions(P2PClientcryonero *);

	static const std::unordered_map<std::string, HTTPHandlerFunction> m_http_handlers;
	static std::unordered_map<std::string, JSONRPCHandlerFunction> m_jsonrpc_handlers;
};

}  // namespace cryonero
