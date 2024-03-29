// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#pragma once

#include <boost/optional.hpp>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include "Core/Difficulty.hpp"
#include "CryptoNote.hpp"
#include "common/Int128.hpp"
#include "crypto/types.hpp"
#include "http/JsonRpc.hpp"

namespace cryonerocoin 
{
	namespace api 
	{
		struct EmptyStruct {};  

		using HeightOrDepth = int32_t;
		constexpr HeightOrDepth DEFAULT_CONFIRMATIONS = 6;

		struct Output
		{
			Amount amount = 0;
			PublicKey public_key;
			uint32_t global_index = 0;
			UnlockMoment unlock_time = 0;  
			uint32_t index_in_transaction = 0;  
			Height height = 0;
			KeyImage key_image;
			PublicKey transaction_public_key;
			std::string address;
			bool dust = false;
		};

		struct Transfer 
		{
			std::string address;
			SignedAmount amount = 0;  
			bool ours = false;       
			bool locked = false;  
			std::vector<api::Output> outputs;
		};

		struct Transaction 
		{
			UnlockMoment unlock_time = 0;    
			std::vector<api::Transfer> transfers;  
			Hash payment_id;        
			uint32_t anonymity = 0; 
			Hash hash;
			SignedAmount fee = 0;
			PublicKey public_key;
			BinaryArray extra; 
			bool coinbase = false;
			Amount amount = 0; 
			Height block_height = 0;
			Hash block_hash;   
			Timestamp timestamp = 0;  
			uint32_t binary_size = 0;
		};

		struct BlockHeader 
		{
			uint8_t major_version = 0;
			uint8_t minor_version = 0;
			Timestamp timestamp = 0;
			Hash previous_block_hash;
			uint32_t nonce = 0;
			Height height = 0;
			Hash hash;
			Amount reward = 0;
			Difficulty cumulative_difficulty = 0;
			Difficulty difficulty = 0;
			Amount base_reward = 0;
			uint32_t block_size = 0;  
			uint32_t transactions_cumulative_size = 0; 
			Amount already_generated_coins = 0;
			uint64_t already_generated_transactions = 0;
			uint32_t size_median = 0;
			uint32_t effective_size_median = 0;
			Timestamp timestamp_median = 0;
			Amount total_fee_amount = 0;

			double penalty() const 
			{
				return base_reward == 0 ? 0 : double(base_reward - reward) / base_reward;
			}  
		};

		struct Block
		{
			api::BlockHeader header;
			std::vector<api::Transaction> transactions;
		};

		struct Balance 
		{
			common::Uint128 spendable = 0;
			common::Uint128 spendable_dust = 0;
			common::Uint128 locked_or_unconfirmed = 0;
			uint64_t spendable_outputs = 0;
			uint64_t spendable_dust_outputs = 0;
			uint64_t locked_or_unconfirmed_outputs = 0;
			common::Uint128 total() const 
			{
				return spendable + spendable_dust + locked_or_unconfirmed;
			} 
			uint64_t total_outputs() const
			{
				return spendable_outputs + spendable_dust_outputs + locked_or_unconfirmed_outputs;
			}
			bool operator==(const Balance &other) const 
			{
				return std::tie(spendable, spendable_dust, locked_or_unconfirmed) == std::tie(other.spendable, other.spendable_dust, other.locked_or_unconfirmed);
			}
			bool operator!=(const Balance &other) const { return !(*this == other); }
		};
	}
}

namespace cryonerocoin
{
	namespace api
	{

		enum return_code {
			CRYONEROD_DATABASE_ERROR = 101,  
			CRYONEROD_ALREADY_RUNNING = 102,
			WALLETD_BIND_PORT_IN_USE = 103,
			CRYONEROD_BIND_PORT_IN_USE = 104,
			CRYONEROD_WRONG_ARGS = 105,
			WALLET_FILE_READ_ERROR = 205,
			WALLET_FILE_UNKNOWN_VERSION = 206,
			WALLET_FILE_DECRYPT_ERROR = 207,
			WALLET_FILE_WRITE_ERROR = 208,
			WALLET_FILE_EXISTS = 209, 
			WALLET_WITH_SAME_KEYS_IN_USE =	210,  
			WALLETD_WRONG_ARGS = 211,
			WALLETD_EXPORTKEYS_MORETHANONE = 212  
		};

		namespace walletd {

			inline std::string url() { return "/json_rpc"; }

			const uint32_t DEFAULT_ANONYMITY_LEVEL = 6;

			struct GetStatus 
			{
				static std::string method() { return "get_status"; }  

				struct Request
				{
					Hash top_block_hash;
					uint32_t transaction_pool_version = 0; 
					uint32_t outgoing_peer_count = 0;
					uint32_t incoming_peer_count = 0;
					std::string	lower_level_error;  

					bool operator==(const Request &other) const 
					{
						return lower_level_error == other.lower_level_error && top_block_hash == other.top_block_hash &&
							transaction_pool_version == other.transaction_pool_version &&
							outgoing_peer_count == other.outgoing_peer_count && incoming_peer_count == other.incoming_peer_count;
					}
					bool operator!=(const Request &other) const { return !operator==(other); }
				};
				struct Response : public Request 
				{  
					Height top_block_height = 0;
					Height top_known_block_height = 0; 
					Difficulty top_block_difficulty = 0;
					Amount recommended_fee_per_byte = 0;
					Timestamp top_block_timestamp = 0;
					Timestamp top_block_timestamp_median = 0;  
					uint32_t next_block_effective_median_size =	0;  
				};
			};

			struct GetAddresses
			{  
				static std::string method() { return "get_addresses"; }

				using Request = EmptyStruct;

				struct Response 
				{
					std::vector<std::string> addresses;
					bool view_only = false;
				};
			};

			struct GetViewKeyPair
			{
				static std::string method() { return "get_view_key_pair"; }

				using Request = EmptyStruct;

				struct Response 
				{
					SecretKey secret_view_key;
					PublicKey public_view_key;
				};
			};

			struct CreateAddresses
			{
				static std::string method() { return "create_addresses"; }
				struct Request
				{
					std::vector<SecretKey> secret_spend_keys; 
					Timestamp creation_timestamp = 0;  
				};
				struct Response 
				{
					std::vector<std::string> addresses;
					std::vector<SecretKey> secret_spend_keys; 
				};
			};

			struct GetBalance 
			{
				static std::string method() { return "get_balance"; }

				struct Request
				{
					std::string address;  
					HeightOrDepth height_or_depth = -DEFAULT_CONFIRMATIONS - 1;
				};
				using Response = api::Balance;
			};

			struct GetUnspents {
				static std::string method() { return "get_unspents"; }
				struct Request {
					std::string address;  
					HeightOrDepth height_or_depth = -DEFAULT_CONFIRMATIONS - 1;
				};
				struct Response {
					std::vector<api::Output> spendable;         
					std::vector<api::Output> locked_or_unconfirmed;  
				};
			};

			struct GetTransfers {  // Can be used incrementally by high-performace clients to monitor incoming transfers
				static std::string method() { return "get_transfers"; }

				struct Request {
					std::string address;                                        // empty for all addresses
					Height from_height = 0;                                     // From, but not including from_height
					Height to_height = std::numeric_limits<uint32_t>::max();  // Up to, and including to_height. Will return
																				// transfers in mempool if to_height >
																				// top_block_height
					bool forward = true;  // determines order of blocks returned, additionally if desired_transactions_count set,
										  // then this defines if call starts from from_height forward, or from to_height backwards
					uint32_t desired_transactions_count =
						std::numeric_limits<uint32_t>::max();  // Will return this number of transactions or a bit more, It can
															   // return more, because this call always returns full blocks
				};
				struct Response {
					std::vector<api::Block> blocks;  // includes only blocks with transactions with transfers we can view
					std::vector<api::Transfer> unlocked_transfers;  // Previous transfers unlocked between from_height and to_height
					Height next_from_height = 0;  // When desired_transactions_count != max you can pass next* to corresponding
												  // Request fields to continue iteration
					Height next_to_height = 0;
				};
			};

			struct CreateTransaction {
				static std::string method() { return "create_transaction"; }

				struct Request {
					api::Transaction transaction;  // You fill only basic info (anonymity, optional unlock_time, optional
												   // payment_id) and transfers. All positive transfers (amount > 0) will be added
												   // as outputs. For all negative transfers (amount < 0), spendable for requested
												   // sum and address will be selected and added as inputs
					std::vector<std::string> spend_addresses;
					// If this is not empty, will spend (and optimize) outputs for this addresses to get
										  // neccessary funds. Otherwise will spend any output in the wallet
					bool any_spend_address = false;  // if you set spend_address to empty, you should set any_spend_address to true.
													 // This is protection against client bug when spend_address is forgotten or
													 // accidentally set to null, etc
					std::string change_address;      // Change will be returned to change_address.
					HeightOrDepth confirmed_height_or_depth = -DEFAULT_CONFIRMATIONS - 1;
					// Mix-ins will be selected from the [0..confirmed_height] window.
					// Reorganizations larger than confirmations may change mix-in global indices, making transaction invalid.
					SignedAmount fee_per_byte = 0;   // Fee of created transaction will be close to the size of tx * fee_per_byte.
													 // You can check it in response.transaction.fee before sending, if you wish
					std::string optimization;  // Wallet outputs optimization (fusion). Leave empty to use normal optimization, good
											   // for wallets with balanced sends to receives count. You can save on a few percent
											   // of fee (on average) by specifying "minimal" for wallet receiving far less
											   // transactions than sending. You should use "aggressive" for wallet receiving far
											   // more transactions than sending, this option will use every opportunity to reduce
											   // number of outputs. For better optimization use as little anonymity as possible. If
											   // anonymity is set to 0, wallet will prioritize optimizing out dust and crazy (large
											   // but not round) denominations of outputs.
					bool save_history = true;  // If true, wallet will save encrypted transaction data (~100 bytes per used address)
											   // in <wallet_file>.history/. With this data it is possible to generate
											   // public-checkable proofs of sending funds to specific addresses.
					std::vector<Hash> prevent_conflict_with_transactions;
					// Experimental API for guaranteed payouts under any circumstances
				};
				struct Response {
					BinaryArray binary_transaction;  // Empty if error
					api::Transaction transaction;
					// block_hash will be empty, block_height set to current pool height (may change later)
					bool save_history_error = false;          // When wallet on read-only media. Most clients should ignore this
					std::vector<Hash> transactions_required;  // Works together with prevent_conflict_with_transactions
					// If not empty, you should resend those transactions before trying create_transaction again to prevent
					// conflicts
				};
				enum {
					NOT_ENOUGH_FUNDS = -301,
					TRANSACTION_DOES_NOT_FIT_IN_BLOCK = -302,  // Sender will have to split funds into several transactions
					NOT_ENOUGH_ANONYMITY = -303
				};
				using Error = json_rpc::Error;
			};

			struct SendTransaction {
				static std::string method() { return "send_transaction"; }

				struct Request {
					BinaryArray binary_transaction;
				};

				struct Response {
					std::string send_result;  // DEPRECATED, always contains "broadcast"
					// when this method returns, transactions is already added to payment queue and queue fsynced to disk.
				};
				enum {
					INVALID_TRANSACTION_BINARY_FORMAT = -101,  // transaction failed to parse
					WRONG_OUTPUT_REFERENCE = -102,  // wrong signature or referenced outputs changed during reorg. Bad output
					// height is reported in conflict_height. If output index > max current index, conflict_height will// be set to
					// currency.max_block_number
					OUTPUT_ALREADY_SPENT = -103
				};  // conflight height reported in error
				struct Error : public json_rpc::Error {
					Height conflict_height = 0;
				};
			};

			struct CreateSendProof {
				static std::string method() { return "create_sendproof"; }

				struct Request {
					Hash transaction_hash;
					std::string message;  // Add any user message to proof. Changing message will invlidate proof (which works like
										  // digital signature of message)
					std::vector<std::string> addresses;  // Leave empty to create proof for all "not ours" addresses
				};

				struct Response {
					std::vector<std::string> sendproofs;
				};
			};

			struct GetTransaction {
				static std::string method() { return "get_transaction"; }
				struct Request {
					Hash hash;
				};
				struct Response {
					api::Transaction
						transaction;  // empty transaction no hash returned if this transaction contains no recognizable transfers
				};
			};
		}
	}
}

// These messages encoded in JSON can be sent via http url /json_rpc3 to cryonerod rpc address:port
// or to binMethod() url encoded in unspecified binary format
namespace cryonerocoin {
	namespace api {
		namespace cryonerod {

			inline std::string url() { return "/json_rpc"; }
			inline std::vector<std::string> legacy_bin_methods() { return { "/sync_mem_pool.bin", "/sync_blocks.bin" }; }
			// When we advance method versions, we add legacy version here to get "upgrade cryonerod" message in walletd"

			struct GetStatus {
				static std::string method() { return "get_node_status"; }  // getNodeStatus works directly or through wallet tunnel
				static std::string method2() { return "get_status"; }
				// getStatus gets either status of node (if called on node) or wallet (if called on wallet)

				using Request = walletd::GetStatus::Request;
				using Response = walletd::GetStatus::Response;
			};

			struct GetRawBlock {
				static std::string method() { return "get_raw_block"; }
				struct Request {
					Hash hash;
				};
				struct Response {
					api::BlockHeader header;
					BlockTemplate raw_header;
					std::vector<TransactionPrefix> raw_transactions;
					Hash base_transaction_hash;                         // BlockTemplate does not contain it
					std::vector<std::vector<uint32_t>> global_indices;  // for each transaction, not empty only if block in main chain
					std::vector<uint32_t> transaction_binary_sizes;     // for each transaction
				};
			};

			struct SyncBlocks {  // Used by walletd, block explorer, etc to sync to cryonerod
				static std::string method() { return "sync_blocks"; }
				static std::string bin_method() { return "/sync_blocks_v1.bin"; }
				// we increment method version when binary format changes

				struct Request {
					static constexpr uint32_t MAX_COUNT = 1000;
					std::vector<Hash> sparse_chain;
					Timestamp first_block_timestamp = 0;
					uint32_t max_count = MAX_COUNT / 10;
				};

				struct Response {
					std::vector<GetRawBlock::Response> blocks;
					Height start_height = 0;
					GetStatus::Response status;  // We save roundtrip during sync by also sending status here
				};
			};

			// TODO - return json error
			struct GetRawTransaction {
				static std::string method() { return "get_raw_transaction"; }
				struct Request {
					Hash hash;
				};
				struct Response {
					api::Transaction transaction;
					// only hash, block_height, block_hash, binary_size, fee returned in transaction
					// empty transaction with no hash returned if not in blockchain/mempool
					TransactionPrefix raw_transaction;
				};
			};

			// Signature of this method will stabilize to the end of beta
			struct SyncMemPool {  // Used by walletd sync process
				static std::string method() { return "sync_mem_pool"; }
				static std::string bin_method() { return "/sync_mem_pool_v1.bin"; }
				// we increment method version when binary format changes
				struct Request {
					std::vector<Hash> known_hashes;  // Should be sent sorted
				};
				struct Response {
					std::vector<Hash> removed_hashes;                                 // Hashes no more in pool
					std::vector<TransactionPrefix> added_raw_transactions;  // New raw transactions in pool
					std::vector<api::Transaction> added_transactions;
					// binary version of this method returns only hash, timestamp, binary_size, and fee here
					GetStatus::Response status;  // We save roundtrip during sync by also sending status here
				};
			};

			struct GetRandomOutputs {
				static std::string method() { return "get_random_outputs"; }
				struct Request {
					std::vector<Amount> amounts;  // Repeating the same amount will give you multiples of outs_count in result
					uint32_t outs_count = 0;
					HeightOrDepth confirmed_height_or_depth = -DEFAULT_CONFIRMATIONS - 1;
					// Mix-ins will be selected from the [0..confirmed_height] window.
													 // Reorganizations larger than confirmations may change mix-in global indices,
													 // making transaction invalid
				};
				struct Response {
					std::map<Amount, std::vector<api::Output>> outputs;
					// can have less outputs than asked for some amounts, if blockchain lacks enough
				};
			};

			using SendTransaction = walletd::SendTransaction;

			struct CheckSendProof {
				static std::string method() { return "check_sendproof"; }

				struct Request {
					std::string sendproof;
				};
				using Response = EmptyStruct; // All errors are reported as json rpc errors
				enum {
					FAILED_TO_PARSE = -201,
					NOT_IN_MAIN_CHAIN = -202,
					WRONG_SIGNATURE = -203,
					ADDRESS_NOT_IN_TRANSACTION = -204,
					WRONG_AMOUNT = -205
				};
				using Error = json_rpc::Error;
			};

			// Methods below are used by miners
			struct GetBlockTemplate {
				static std::string method_legacy() { return "getblocktemplate"; }  // This name is used by old miners
				static std::string method() { return "get_block_template"; }
				struct Request {
					uint32_t reserve_size = 0;  // max 255 bytes
					std::string wallet_address;
					Hash top_block_hash;                    // for longpoll in v3 - behaves like GetStatus
					uint32_t transaction_pool_version = 0;  // for longpoll in v3 - behaves like GetStatus
				};
				struct Response {
					Difficulty difficulty = 0;
					Height height = 0;
					uint32_t reserved_offset = 0;
					BinaryArray blocktemplate_blob;
					std::string status;
					Hash top_block_hash;                    // for longpoll in v3 - behaves like GetStatus
					uint32_t transaction_pool_version = 0;  // for longpoll in v3 - behaves like GetStatus
					Hash previous_block_hash;               // Deprecated, used by some legacy miners.
				};
			};

			struct GetCurrencyId {
				static std::string method_legacy() { return "getcurrencyid"; }  // This name is used by old miners
				static std::string method() { return "get_currency_id"; }
				using Request = EmptyStruct;
				struct Response {
					Hash currency_id_blob;  // hash of genesis block
				};
			};

			struct SubmitBlock {
				static std::string method() { return "submit_block"; }
				struct Request {
					BinaryArray blocktemplate_blob;
				};
				struct Response {
					std::string status;
				};
			};

			// Legacy methods
			struct SubmitBlockLegacy {
				static std::string method() { return "submitblock"; }  // This name is used by old miners
				using Request = std::vector<std::string>;
				using Response = SubmitBlock::Response;
			};

			struct BlockHeaderLegacy : public api::BlockHeader {
				bool orphan_status = false;
				HeightOrDepth depth = 0;
			};
			struct GetLastBlockHeaderLegacy {  // Use GetStatus instead
				static std::string method() { return "getlastblockheader"; }
				using Request = EmptyStruct;
				struct Response {
					std::string status;
					BlockHeaderLegacy block_header;
				};
			};

			struct GetBlockHeaderByHashLegacy {
				static std::string method() { return "getblockheaderbyhash"; }
				struct Request {
					Hash hash;
				};
				using Response = GetLastBlockHeaderLegacy::Response;
			};

			struct GetBlockHeaderByHeightLegacy {
				static std::string method() { return "getblockheaderbyheight"; }
				struct Request {
					Height height = 0;  // Beware, in this call height starts from 1, not 0, so height=1 returns genesis
				};
				using Response = GetLastBlockHeaderLegacy::Response;
			};
		}
	}
}

namespace seria {

	class ISeria;

	void ser_members(cryonerocoin::api::EmptyStruct &v, ISeria &s);
	void ser_members(cryonerocoin::api::Output &v, ISeria &s);
	void ser_members(cryonerocoin::api::BlockHeader &v, ISeria &s);
	void ser_members(cryonerocoin::api::Transfer &v, ISeria &s);
	void ser_members(cryonerocoin::api::Transaction &v, ISeria &s);
	void ser_members(cryonerocoin::api::Block &v, ISeria &s);
	void ser_members(cryonerocoin::api::Balance &v, ISeria &s);

	void ser_members(cryonerocoin::api::walletd::GetAddresses::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetViewKeyPair::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::CreateAddresses::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::CreateAddresses::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetBalance::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetBalance::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetUnspents::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetUnspents::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetTransfers::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetTransfers::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::CreateTransaction::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::CreateTransaction::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::CreateSendProof::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::CreateSendProof::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetTransaction::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::walletd::GetTransaction::Response &v, ISeria &s);

	void ser_members(cryonerocoin::api::cryonerod::GetStatus::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetStatus::Response &v, ISeria &s);

	void ser_members(cryonerocoin::api::cryonerod::GetRawBlock::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetRawBlock::Response &v, ISeria &s);

	void ser_members(cryonerocoin::api::cryonerod::SyncBlocks::Request &v, ISeria &s);
	//void ser_members(cryonerocoin::api::cryonerod::SyncBlocks::SyncBlock &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::SyncBlocks::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetRawTransaction::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetRawTransaction::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::SyncMemPool::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::SyncMemPool::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetRandomOutputs::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetRandomOutputs::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::SendTransaction::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::SendTransaction::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::SendTransaction::Error &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::CheckSendProof::Request &v, ISeria &s);
	// void ser_members(cryonerocoin::api::cryonerod::CheckSendProof::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetBlockTemplate::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetBlockTemplate::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetCurrencyId::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::SubmitBlock::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::SubmitBlock::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::BlockHeaderLegacy &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetLastBlockHeaderLegacy::Response &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetBlockHeaderByHashLegacy::Request &v, ISeria &s);
	void ser_members(cryonerocoin::api::cryonerod::GetBlockHeaderByHeightLegacy::Request &v, ISeria &s);

}  // namespace seria
