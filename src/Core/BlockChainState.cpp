// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include "BlockChainState.hpp"
#include <condition_variable>
#include <random>
#include "Config.hpp"
#include "CryptoNoteTools.hpp"
#include "Currency.hpp"
#include "TransactionExtra.hpp"
#include "common/Math.hpp"
#include "common/StringTools.hpp"
#include "common/Varint.hpp"
#include "crypto/crypto.hpp"
#include "platform/Time.hpp"
#include "seria/BinaryInputStream.hpp"
#include "seria/BinaryOutputStream.hpp"

static const std::string KEYIMAGE_PREFIX = "i";
static const std::string AMOUNT_OUTPUT_PREFIX = "a";
static const std::string BLOCK_GLOBAL_INDICES_PREFIX = "b";
static const std::string BLOCK_GLOBAL_INDICES_SUFFIX = "g";

static const std::string UNLOCK_BLOCK_PREFIX = "u";
static const std::string UNLOCK_TIME_PREFIX = "U";


const size_t MAX_POOL_SIZE = 2000000;

using namespace cryonerocoin;
using namespace platform;


namespace seria {
	void ser_members(IBlockChainState::UnlockTimePublickKeyHeightSpent &v, ISeria &s) {
		seria_kv("unlock_time", v.unlock_time, s);
		seria_kv("public_key", v.public_key, s);
		seria_kv("height", v.height, s);
		seria_kv("spent", v.spent, s);
	}
}

BlockChainState::PoolTransaction::PoolTransaction(const Transaction &tx, const BinaryArray &binary_tx, Amount fee, Timestamp timestamp)
	: tx(tx), binary_tx(binary_tx), fee(fee), timestamp(timestamp) {}

void BlockChainState::DeltaState::store_keyimage(const KeyImage &key_image, Height height) {
	invariant(m_keyimages.insert(std::make_pair(key_image, height)).second, common::pod_to_hex(key_image));
}

void BlockChainState::DeltaState::delete_keyimage(const KeyImage &key_image) {
	invariant(m_keyimages.erase(key_image) == 1, common::pod_to_hex(key_image));
}

bool BlockChainState::DeltaState::read_keyimage(const KeyImage &key_image, Height *height) const {
	auto kit = m_keyimages.find(key_image);
	if (kit == m_keyimages.end())
		return m_parent_state->read_keyimage(key_image, height);
	*height = m_block_height;
	return true;
}

uint32_t BlockChainState::DeltaState::push_amount_output(
	Amount amount, UnlockMoment unlock_time, Height block_height, const PublicKey &pk) {
	uint32_t pg = m_parent_state->next_global_index_for_amount(amount);
	auto &ga = m_global_amounts[amount];
	ga.push_back(std::make_pair(unlock_time, pk));
	return pg + static_cast<uint32_t>(ga.size()) - 1;
}

void BlockChainState::DeltaState::pop_amount_output(Amount amount, UnlockMoment unlock_time, const PublicKey &pk) {
	std::vector<std::pair<uint64_t, PublicKey>> &el = m_global_amounts[amount];
	invariant(!el.empty(), "DeltaState::pop_amount_output underflow");
	invariant(el.back().first == unlock_time && el.back().second == pk, "DeltaState::pop_amount_output wrong element");
	el.pop_back();
}

uint32_t BlockChainState::DeltaState::next_global_index_for_amount(Amount amount) const {
	uint32_t pg = m_parent_state->next_global_index_for_amount(amount);
	auto git = m_global_amounts.find(amount);
	return (git == m_global_amounts.end()) ? pg : static_cast<uint32_t>(git->second.size()) + pg;
}

bool BlockChainState::DeltaState::read_amount_output(Amount amount, uint32_t global_index, UnlockTimePublickKeyHeightSpent *unp) const {
	uint32_t pg = m_parent_state->next_global_index_for_amount(amount);
	if (global_index < pg)
		return m_parent_state->read_amount_output(amount, global_index, unp);
	global_index -= pg;
	auto git = m_global_amounts.find(amount);
	if (git == m_global_amounts.end() || global_index >= git->second.size())
		return false;
	unp->unlock_time = git->second[global_index].first;
	unp->public_key = git->second[global_index].second;
	unp->height = m_block_height;
	unp->spent = false;  // Spending just created outputs inside mempool or block is prohibited, simplifying logic
	return true;
}
void BlockChainState::DeltaState::spend_output(Amount amount, uint32_t global_index) {
	m_spent_outputs.push_back(std::make_pair(amount, global_index));
}

void BlockChainState::DeltaState::apply(IBlockChainState *parent_state) const {
	for (auto &&ki : m_keyimages)
		parent_state->store_keyimage(ki.first, ki.second);
	for (auto &&amp : m_global_amounts)
		for (auto &&el : amp.second)
			parent_state->push_amount_output(amp.first, el.first, m_block_height, el.second);
	for (auto &&mo : m_spent_outputs)
		parent_state->spend_output(mo.first, mo.second);
}

void BlockChainState::DeltaState::clear(Height new_block_height) {
	m_block_height = new_block_height;
	m_keyimages.clear();
	m_global_amounts.clear();
	m_spent_outputs.clear();
}

api::BlockHeader BlockChainState::fill_genesis(Hash genesis_bid, const BlockTemplate &g) {
	api::BlockHeader result;
	result.major_version = g.major_version;
	result.minor_version = g.minor_version;
	result.previous_block_hash = g.previous_block_hash;
	result.timestamp = g.timestamp;
	result.nonce = g.nonce;
	result.hash = genesis_bid;
	return result;
}

static std::string validate_semantic(bool generating, const Transaction &tx, uint64_t *fee, bool check_output_key) {
	if (tx.inputs.empty())
		return "EMPTY_INPUTS";
	uint64_t summary_output_amount = 0;
	for (const auto &output : tx.outputs) {
		if (output.amount == 0)
			return "OUTPUT_ZERO_AMOUNT";
		if (output.target.type() == typeid(KeyOutput)) {
			const KeyOutput &key_output = boost::get<KeyOutput>(output.target);
			if (check_output_key && !key_isvalid(key_output.key))
				return "OUTPUT_INVALID_KEY";
		}
		else
			return "OUTPUT_UNKNOWN_TYPE";
		if (std::numeric_limits<uint64_t>::max() - output.amount < summary_output_amount)
			return "OUTPUTS_AMOUNT_OVERFLOW";
		summary_output_amount += output.amount;
	}
	uint64_t summary_input_amount = 0;
	std::unordered_set<KeyImage> ki;
	std::set<std::pair<uint64_t, uint32_t>> outputs_usage;
	for (const auto &input : tx.inputs) {
		uint64_t amount = 0;
		if (input.type() == typeid(CoinbaseInput)) {
			if (!generating)
				return "INPUT_UNKNOWN_TYPE";
		}
		else if (input.type() == typeid(KeyInput)) {
			if (generating)
				return "INPUT_UNKNOWN_TYPE";
			const KeyInput &in = boost::get<KeyInput>(input);
			amount = in.amount;
			if (!ki.insert(in.key_image).second)
				return "INPUT_IDENTICAL_KEYIMAGES";
			if (in.output_indexes.empty())
				return "INPUT_EMPTY_OUTPUT_USAGE";

			if (std::find(++std::begin(in.output_indexes), std::end(in.output_indexes), 0) !=
				std::end(in.output_indexes)) {
				return "INPUT_IDENTICAL_OUTPUT_INDEXES";
			}
		}
		else
			return "INPUT_UNKNOWN_TYPE";
		if (std::numeric_limits<uint64_t>::max() - amount < summary_input_amount)
			return "INPUTS_AMOUNT_OVERFLOW";
		summary_input_amount += amount;
	}
	if (summary_output_amount > summary_input_amount && !generating)
		return "WRONG_AMOUNT";
	if (tx.signatures.size() != tx.inputs.size() && !generating)
		return "INPUT_UNKNOWN_TYPE";
	if (!tx.signatures.empty() && generating)
		return "INPUT_UNKNOWN_TYPE";
	*fee = summary_input_amount - summary_output_amount;
	return std::string();
}

BlockChainState::BlockChainState(logging::ILogger &log, const Config &config, const Currency &currency, bool read_only)
	: BlockChain(log, config, currency, read_only), log_redo_block_timestamp(std::chrono::steady_clock::now()) {




	std::string version;
	m_db.get("$version", version);
	if (version == "B" || version == "1" || version == "2" || version == "3" || version == "4") {
		start_internal_import();
		version = version_current;
		m_db.put("$version", version, false);
		db_commit();
	}
	if (version != version_current)
		throw std::runtime_error("Blockchain database format unknown (version=" + version + "), please delete " +
			config.get_data_folder() + "/blockchain");
	if (get_tip_height() == (Height)-1) {
		Block genesis_block;
		genesis_block.header = currency.genesis_block_template;
		RawBlock raw_block;
		invariant(genesis_block.to_raw_block(raw_block), "Genesis block failed to convert into raw block");
		PreparedBlock pb(std::move(raw_block), nullptr);
		api::BlockHeader info;
		invariant(add_block(pb, &info, std::string()) != BroadcastAction::BAN, "Genesis block failed to add");
	}
	BlockChainState::tip_changed();
	m_log(logging::INFO) << "BlockChainState::BlockChainState height=" << get_tip_height()
		<< " cumulative_difficulty=" << get_tip_cumulative_difficulty() << " bid=" << get_tip_bid()
		<< std::endl;
}

std::string BlockChainState::check_standalone_consensus(
	const PreparedBlock &pb, api::BlockHeader *info, const api::BlockHeader &prev_info, bool check_pow) const {
	const auto &block = pb.block;
	if (block.transactions.size() != block.header.transaction_hashes.size() ||
		block.transactions.size() != pb.raw_block.transactions.size())
		return "WRONG_TRANSACTIONS_COUNT";
	info->size_median = m_next_median_size;
	info->timestamp_median = m_next_median_timestamp;

	if (get_tip_bid() != prev_info.hash)
		calculate_consensus_values(prev_info, &info->size_median, &info->timestamp_median);

	auto next_block_granted_full_reward_zone = m_currency.block_granted_full_reward_zone_by_block_version(
		block.header.major_version);
	info->effective_size_median = std::max(info->size_median, next_block_granted_full_reward_zone);

	size_t cumulative_size = 0;
	for (size_t i = 0; i != pb.raw_block.transactions.size(); ++i) {
		if (pb.raw_block.transactions.at(i).size() > m_currency.max_transaction_allowed_size(info->effective_size_median)) {

			return "RAW_TRANSACTION_SIZE_TOO_BIG";
		}
		cumulative_size += pb.raw_block.transactions.at(i).size();
		Hash tid = get_transaction_hash(pb.block.transactions.at(i));
		if (tid != pb.block.header.transaction_hashes.at(i))
			return "TRANSACTION_ABSENT_IN_POOL";
	}
	info->block_size = static_cast<uint32_t>(pb.coinbase_tx_size + cumulative_size);
	auto max_block_cumulative_size = m_currency.max_block_cumulative_size(info->height);
	if (info->block_size > max_block_cumulative_size)
		return "CUMULATIVE_BLOCK_SIZE_TOO_BIG";

	// block at UPGRADE_HEIGHT still has old version.
	if (block.header.major_version != m_currency.get_block_major_version_for_height(info->height))
		return "WRONG_VERSION";

	if (block.header.major_version >= 2) {
		if (block.header.major_version == 2 && block.header.parent_block.major_version > 1)
			return "PARENT_BLOCK_WRONG_VERSION";
		size_t pasi = pb.parent_block_size;
		if (pasi > 2048)
			return "PARENT_BLOCK_SIZE_TOO_BIG";
	}
	auto now = platform::now_unix_timestamp();  // It would be better to pass now through Node
	if (block.header.timestamp > now + m_currency.get_block_future_time_limit(get_tip_height() + 1))
		return "TIMESTAMP_TOO_FAR_IN_FUTURE";
	if (block.header.timestamp < info->timestamp_median)
		return "TIMESTAMP_TOO_FAR_IN_PAST";

	if (block.header.base_transaction.inputs.size() != 1)
		return "INPUT_WRONG_COUNT";

	if (block.header.base_transaction.inputs[0].type() != typeid(CoinbaseInput))
		return "INPUT_UNEXPECTED_TYPE";

	if (boost::get<CoinbaseInput>(block.header.base_transaction.inputs[0]).block_index != info->height)
		return "BASE_INPUT_WRONG_BLOCK_INDEX";

	if (block.header.base_transaction.unlock_time != info->height + m_currency.mined_money_unlock_window)
		return "WRONG_TRANSACTION_UNLOCK_TIME";

	const bool check_keys = !m_currency.is_in_sw_checkpoint_zone(info->height);
	uint64_t miner_reward = 0;
	for (const auto &output : block.header.base_transaction.outputs) {  // TODO - call validate_semantic
		if (output.amount == 0)
			return "OUTPUT_ZERO_AMOUNT";
		if (output.target.type() == typeid(KeyOutput)) {
			if (check_keys && !key_isvalid(boost::get<KeyOutput>(output.target).key))
				return "OUTPUT_INVALID_KEY";
		}
		else
			return "OUTPUT_UNKNOWN_TYPE";

		if (std::numeric_limits<uint64_t>::max() - output.amount < miner_reward)
			return "OUTPUTS_AMOUNT_OVERFLOW";
		miner_reward += output.amount;
	}
	{
		std::vector<Timestamp> timestamps;
		std::vector<Difficulty> difficulties;
		Height blocks_count = std::min(prev_info.height, m_currency.get_difficulty_blocks_count(get_tip_height() + 1));
		auto timestamps_window = get_tip_segment(prev_info, blocks_count, false);
		size_t actual_count = timestamps_window.size();
		timestamps.resize(actual_count);
		difficulties.resize(actual_count);
		size_t pos = 0;
		for (auto it = timestamps_window.begin(); it != timestamps_window.end(); ++it, ++pos) {
			timestamps.at(pos) = it->timestamp;
			difficulties.at(pos) = it->cumulative_difficulty;
		}
		info->difficulty = m_currency.next_difficulty(prev_info.height, timestamps, difficulties);
		info->cumulative_difficulty = prev_info.cumulative_difficulty + info->difficulty;
	}

	if (info->difficulty == 0)
		return "DIFFICULTY_OVERHEAD";

	Amount cumulative_fee = 0;
	for (const auto &tx : block.transactions) {
		Amount fee = 0;
		if (!get_tx_fee(tx, &fee))
			return "WRONG_AMOUNT";
		cumulative_fee += fee;
	}

	int64_t emission_change = 0;
	auto already_generated_coins = prev_info.already_generated_coins;

	if (!m_currency.get_block_reward(block.header.major_version, info->effective_size_median, 0, already_generated_coins, 0, &info->base_reward, &emission_change) ||
		!m_currency.get_block_reward(block.header.major_version, info->effective_size_median, info->block_size,
			already_generated_coins, cumulative_fee, &info->reward, &emission_change)) {
		return "CUMULATIVE_BLOCK_SIZE_TOO_BIG";
	}

	if (miner_reward != info->reward) {

		return "BLOCK_REWARD_MISMATCH";
	}
	info->already_generated_coins = prev_info.already_generated_coins + emission_change;
	info->already_generated_transactions = prev_info.already_generated_transactions + block.transactions.size() + 1;
	info->total_fee_amount = cumulative_fee;
	info->transactions_cumulative_size = static_cast<uint32_t>(cumulative_size);
	for (auto &&tx : pb.block.transactions) {
		Amount tx_fee = 0;
		std::string tx_result = validate_semantic(false, tx, &tx_fee, check_keys);
		if (!tx_result.empty())
			return tx_result;
	}
	if (m_currency.is_in_sw_checkpoint_zone(info->height)) {
		bool is_checkpoint;
		if (!m_currency.check_sw_checkpoint(info->height, info->hash, is_checkpoint))
			return "CHECKPOINT_BLOCK_HASH_MISMATCH";
	}
	else {
		if (!check_pow)
			return std::string();
		Hash long_hash = pb.long_block_hash != Hash{} ? pb.long_block_hash
			: get_block_long_hash(block.header, m_hash_crypto_context);
		if (!m_currency.check_proof_of_work(long_hash, block.header, info->difficulty))
			return "PROOF_OF_WORK_TOO_WEAK";
	}
	return std::string();
}

void BlockChainState::calculate_consensus_values(const api::BlockHeader &prev_info, uint32_t *next_median_size, Timestamp *next_median_timestamp) const {
	std::vector<uint32_t> last_blocks_sizes;
	auto window = get_tip_segment(prev_info, m_currency.reward_blocks_window, true);
	last_blocks_sizes.reserve(m_currency.reward_blocks_window);
	for (auto it = window.begin(); it != window.end(); ++it)
		last_blocks_sizes.push_back(it->block_size);
	*next_median_size = common::median_value(&last_blocks_sizes);

	window = get_tip_segment(prev_info, m_currency.get_timestamp_check_window(get_tip_height() + 1), false);
	if (window.size() >= m_currency.get_timestamp_check_window(get_tip_height() + 1)) {
		std::vector<Timestamp> timestamps;
		timestamps.reserve(m_currency.get_timestamp_check_window(get_tip_height() + 1));
		for (auto it = window.begin(); it != window.end(); ++it)
			timestamps.push_back(it->timestamp);
		*next_median_timestamp = common::median_value(&timestamps);  // sorts timestamps

	}
	else {
		*next_median_timestamp = 0;

	}
}

void BlockChainState::tip_changed() {
	calculate_consensus_values(get_tip(), &m_next_median_size, &m_next_median_timestamp);
}

bool BlockChainState::create_mining_block_template(BlockTemplate *b, const AccountPublicAddress &adr,
	const BinaryArray &extra_nonce, Difficulty *difficulty, Height *height) const {
	clear_mining_transactions();
	*height = get_tip_height() + 1;
	*b = BlockTemplate{};
	b->major_version = m_currency.get_block_major_version_for_height(*height);
	{
		std::vector<Timestamp> timestamps;
		std::vector<Difficulty> difficulties;
		Height blocks_count = std::min(get_tip_height(), m_currency.get_difficulty_blocks_count(get_tip_height() + 1));
		timestamps.reserve(blocks_count);
		difficulties.reserve(blocks_count);
		auto timestamps_window = get_tip_segment(get_tip(), blocks_count, false);
		for (auto it = timestamps_window.begin(); it != timestamps_window.end(); ++it) {
			timestamps.push_back(it->timestamp);
			difficulties.push_back(it->cumulative_difficulty);
		}
		*difficulty = m_currency.next_difficulty(*height, timestamps, difficulties);
	}
	if (*difficulty == 0) {
		m_log(logging::ERROR) << "difficulty overhead in create_mining_block_template." << std::endl;
		return false;
	}



	if (b->major_version == 1) {
		b->minor_version = m_currency.upgrade_height_v2 == Height(-1) ? 1 : 0;
	}
	else if (b->major_version >= 2) {
		if (m_currency.upgrade_height_v3 == Height(-1)) {
			b->minor_version = (b->major_version == 2) ? 1 : 0;
		}
		else {
			b->minor_version = 0;
		}

		b->parent_block.major_version = 1;
		b->parent_block.minor_version = 0;
		b->parent_block.transaction_count = 1;

		TransactionExtraMergeMiningTag mm_tag{};
		if (!append_merge_mining_tag_to_extra(b->parent_block.base_transaction.extra, mm_tag)) {
			m_log(logging::ERROR) << logging::BrightRed << "Failed to append merge mining tag to extra of "
				"the parent block miner transaction";
			return false;
		}
	}

	b->previous_block_hash = get_tip_bid();
	b->timestamp = std::max(platform::now_unix_timestamp(), m_next_median_timestamp);

	auto next_block_granted_full_reward_zone =
		m_currency.block_granted_full_reward_zone_by_block_version(b->major_version);
	auto effective_size_median = std::max(m_next_median_size, next_block_granted_full_reward_zone);
	Amount already_generated_coins = get_tip().already_generated_coins;

	auto max_total_size = (125 * effective_size_median) / 100;
	auto max_cumulative_size = m_currency.max_block_cumulative_size(*height);
	max_total_size = std::min(max_total_size, max_cumulative_size) - m_currency.miner_tx_blob_reserved_size;

	std::vector<Hash> pool_hashes;
	for (auto &&msf : m_memory_state_fee_tx)
		for (auto &&ha : msf.second)
			pool_hashes.push_back(ha);
	size_t txs_size = 0;
	Amount fee = 0;
	DeltaState memory_state(*height, b->timestamp, this);  // will be get_tip().timestamp_unlock after fork


	for (; !pool_hashes.empty(); pool_hashes.pop_back()) {
		auto tit = m_memory_state_tx.find(pool_hashes.back());
		if (tit == m_memory_state_tx.end()) {
			m_log(logging::ERROR) << "Transaction " << pool_hashes.back() << " is in pool index, but not in pool";
			assert(false);
			continue;
		}
		const size_t block_size_limit = max_total_size;
		const size_t tx_size = tit->second.binary_tx.size();
		if (txs_size + tx_size > block_size_limit)
			continue;
		Amount single_fee = tit->second.fee;
		BlockGlobalIndices global_indices;
		Height conflict_height = 0;
		const std::string result =
			redo_transaction_get_error(false, tit->second.tx, &memory_state, &global_indices, &conflict_height, true);
		if (!result.empty()) {
			m_log(logging::ERROR) << "Transaction " << tit->first
				<< " is in pool, but could not be redone result=" << result << std::endl;
			continue;
		}
		txs_size += tx_size;
		fee += single_fee;
		b->transaction_hashes.emplace_back(tit->first);
		m_mining_transactions.insert(std::make_pair(tit->first, std::make_pair(tit->second.binary_tx, *height)));
		m_log(logging::TRACE) << "Transaction " << tit->first << " included to block template";
	}


	bool r = m_currency.construct_miner_tx(b->major_version, *height, effective_size_median, already_generated_coins,
		txs_size, fee, adr, &b->base_transaction, extra_nonce, 11);
	if (!r) {
		m_log(logging::ERROR) << logging::BrightRed << "Failed to construct miner tx, first chance";
		return false;
	}

	size_t cumulative_size = txs_size + seria::binary_size(b->base_transaction);
	const size_t TRIES_COUNT = 10;
	for (size_t try_count = 0; try_count < TRIES_COUNT; ++try_count) {
		r = m_currency.construct_miner_tx(b->major_version, *height, effective_size_median, already_generated_coins,
			cumulative_size, fee, adr, &b->base_transaction, extra_nonce, 11);
		if (!r) {
			m_log(logging::ERROR) << logging::BrightRed << "Failed to construct miner tx, second chance";
			return false;
		}

		size_t coinbase_blob_size = seria::binary_size(b->base_transaction);
		if (coinbase_blob_size > cumulative_size - txs_size) {
			cumulative_size = txs_size + coinbase_blob_size;
			continue;
		}

		if (coinbase_blob_size < cumulative_size - txs_size) {
			size_t delta = cumulative_size - txs_size - coinbase_blob_size;
			common::append(b->base_transaction.extra, delta, 0);

			if (cumulative_size != txs_size + seria::binary_size(b->base_transaction)) {
				if (cumulative_size + 1 != txs_size + seria::binary_size(b->base_transaction)) {
					m_log(logging::ERROR)
						<< logging::BrightRed << "unexpected case: cumulative_size=" << cumulative_size
						<< " + 1 is not equal txs_cumulative_size=" << txs_size
						<< " + get_object_blobsize(b.base_transaction)=" << seria::binary_size(b->base_transaction);
					return false;
				}

				b->base_transaction.extra.resize(b->base_transaction.extra.size() - 1);
				if (cumulative_size != txs_size + seria::binary_size(b->base_transaction)) {

					m_log(logging::TRACE)
						<< logging::BrightRed << "Miner tx creation have no luck with delta_extra size = " << delta
						<< " and " << delta - 1;
					cumulative_size += delta - 1;
					continue;
				}

				m_log(logging::TRACE) << logging::BrightGreen
					<< "Setting extra for block: " << b->base_transaction.extra.size()
					<< ", try_count=" << try_count;
			}
		}
		if (cumulative_size != txs_size + seria::binary_size(b->base_transaction)) {
			m_log(logging::ERROR) << logging::BrightRed << "unexpected case: cumulative_size=" << cumulative_size
				<< " is not equal txs_cumulative_size=" << txs_size
				<< " + get_object_blobsize(b.base_transaction)="
				<< seria::binary_size(b->base_transaction);
			return false;
		}
		return true;
	}
	m_log(logging::ERROR) << logging::BrightRed << "Failed to create_block_template with " << TRIES_COUNT << " tries";
	return false;
}



uint32_t BlockChainState::get_next_effective_median_size() const {
	uint8_t next_major_version = m_currency.get_block_major_version_for_height(get_tip_height() + 1);
	auto next_block_granted_full_reward_zone =
		m_currency.block_granted_full_reward_zone_by_block_version(next_major_version);
	return std::max(m_next_median_size, next_block_granted_full_reward_zone);
}

BroadcastAction BlockChainState::add_mined_block(
	const BinaryArray &raw_block_template, RawBlock *raw_block, api::BlockHeader *info) {
	BlockTemplate block_template;
	seria::from_binary(block_template, raw_block_template);
	raw_block->block = std::move(raw_block_template);

	raw_block->transactions.reserve(block_template.transaction_hashes.size());
	raw_block->transactions.clear();
	for (const auto &tx_hash : block_template.transaction_hashes) {
		auto tit = m_memory_state_tx.find(tx_hash);
		const BinaryArray *binary_tx = nullptr;
		if (tit != m_memory_state_tx.end())
			binary_tx = &(tit->second.binary_tx);
		else {
			auto tit2 = m_mining_transactions.find(tx_hash);
			if (tit2 == m_mining_transactions.end()) {
				m_log(logging::WARNING) << "The transaction " << tx_hash
					<< " is absent in transaction pool on submit mined block";
				return BroadcastAction::NOTHING;
			}
			binary_tx = &(tit2->second.first);
		}
		raw_block->transactions.emplace_back(*binary_tx);
	}
	PreparedBlock pb(std::move(*raw_block), nullptr);
	*raw_block = pb.raw_block;
	return add_block(pb, info, "json_rpc");
}

void BlockChainState::clear_mining_transactions() const {
	for (auto tit = m_mining_transactions.begin(); tit != m_mining_transactions.end();)
		if (get_tip_height() > tit->second.second + 3)  // Remember txs for 3 blocks
			tit = m_mining_transactions.erase(tit);
		else
			++tit;
}

Amount BlockChainState::minimum_pool_fee_per_byte(Hash *minimal_tid) const {
	if (m_memory_state_fee_tx.empty()) {
		*minimal_tid = Hash();
		return 0;
	}
	auto be = m_memory_state_fee_tx.begin();
	invariant(!be->second.empty(), "Invariant dead, memory_state_fee_tx empty set");
	*minimal_tid = *(be->second.begin());
	return be->first;
}

void BlockChainState::on_reorganization(
	const std::map<Hash, std::pair<Transaction, BinaryArray>> &undone_transactions, bool undone_blocks) {
	Height conflict_height = 0;
	if (undone_blocks) {
		PoolTransMap old_memory_state_tx;
		std::swap(old_memory_state_tx, m_memory_state_tx);
		m_memory_state_ki_tx.clear();
		m_memory_state_fee_tx.clear();
		m_memory_state_total_size = 0;
		for (auto &&msf : old_memory_state_tx) {
			add_transaction(msf.first, msf.second.tx, msf.second.binary_tx, get_tip_height() + 1, get_tip().timestamp,
				&conflict_height, true, std::string());
		}
	}
	for (auto ud : undone_transactions) {
		add_transaction(ud.first, ud.second.first, ud.second.second, get_tip_height() + 1, get_tip().timestamp,
			&conflict_height, true, std::string());
	}
	m_tx_pool_version = 2;  // add_transaction will erroneously increase
}

AddTransactionResult BlockChainState::add_transaction(const Hash &tid, const Transaction &tx,
	const BinaryArray &binary_tx, Timestamp now, Height *conflict_height, const std::string &source_address) {

	return add_transaction(
		tid, tx, binary_tx, get_tip_height() + 1, get_tip().timestamp, conflict_height, true, source_address);
}

AddTransactionResult BlockChainState::add_transaction(const Hash &tid, const Transaction &tx,
	const BinaryArray &binary_tx, Height unlock_height, Timestamp unlock_timestamp, Height *conflict_height,
	bool check_sigs, const std::string &source_address) {
	if (m_memory_state_tx.count(tid) != 0) {
		return AddTransactionResult::ALREADY_IN_POOL;
	}
	const size_t my_size = binary_tx.size();
	const Amount my_fee = cryonerocoin::get_tx_fee(tx);
	const Amount my_fee_per_byte = my_fee / my_size;
	Hash minimal_tid;
	Amount minimal_fee = minimum_pool_fee_per_byte(&minimal_tid);
	if (m_memory_state_total_size >= MAX_POOL_SIZE && my_fee_per_byte < minimal_fee)
		return AddTransactionResult::INCREASE_FEE;
	if (m_memory_state_total_size >= MAX_POOL_SIZE && my_fee_per_byte == minimal_fee && tid < minimal_tid)
		return AddTransactionResult::INCREASE_FEE;
	for (const auto &input : tx.inputs) {
		if (input.type() == typeid(KeyInput)) {
			const KeyInput &in = boost::get<KeyInput>(input);
			auto tit = m_memory_state_ki_tx.find(in.key_image);
			if (tit == m_memory_state_ki_tx.end())
				continue;
			const PoolTransaction &other_tx = m_memory_state_tx.at(tit->second);
			const Amount other_fee_per_byte = other_tx.fee_per_byte();
			if (my_fee_per_byte < other_fee_per_byte)
				return AddTransactionResult::INCREASE_FEE;
			if (my_fee_per_byte == other_fee_per_byte && tid < tit->second)
				return AddTransactionResult::INCREASE_FEE;
			break;  // Can displace another transaction from the pool, Will have to make heavy-lifting for this tx
		}
	}
	for (const auto &input : tx.inputs) {
		if (input.type() == typeid(KeyInput)) {
			const KeyInput &in = boost::get<KeyInput>(input);
			if (read_keyimage(in.key_image, conflict_height)) {

				return AddTransactionResult::OUTPUT_ALREADY_SPENT;  // Already spent in main chain
			}
		}
	}
	Amount my_fee3 = 0;
	const std::string validate_result = validate_semantic(false, tx, &my_fee3, check_sigs);
	if (!validate_result.empty()) {
		m_log(logging::WARNING) << "add_transaction validation failed " << validate_result << " in transaction " << tid
			<< std::endl;
		return AddTransactionResult::BAN;
	}
	DeltaState memory_state(unlock_height, unlock_timestamp, this);
	BlockGlobalIndices global_indices;
	const std::string redo_result =
		redo_transaction_get_error(false, tx, &memory_state, &global_indices, conflict_height, check_sigs);
	if (!redo_result.empty()) {

		m_log(logging::TRACE) << "add_transaction redo failed " << redo_result << " in transaction " << tid
			<< std::endl;
		return AddTransactionResult::FAILED_TO_REDO;  // Not a ban because reorg can change indices
	}
	if (my_fee != my_fee3)
		m_log(logging::ERROR) << "Inconsistent fees " << my_fee << ", " << my_fee3 << " in transaction " << tid
		<< std::endl;

	for (auto &&ki : memory_state.get_keyimages()) {
		auto tit = m_memory_state_ki_tx.find(ki.first);
		if (tit == m_memory_state_ki_tx.end())
			continue;
		const PoolTransaction &other_tx = m_memory_state_tx.at(tit->second);
		const Amount other_fee_per_byte = other_tx.fee_per_byte();
		if (my_fee_per_byte < other_fee_per_byte)
			return AddTransactionResult::INCREASE_FEE;  // Never because checked above
		if (my_fee_per_byte == other_fee_per_byte && tid < tit->second)
			return AddTransactionResult::INCREASE_FEE;  // Never because checked above
		remove_from_pool(tit->second);
	}
	bool all_inserted = true;
	for (auto &&ki : memory_state.get_keyimages()) {
		if (!m_memory_state_ki_tx.insert(std::make_pair(ki.first, tid)).second)
			all_inserted = false;
	}
	if (!m_memory_state_tx.insert(std::make_pair(tid, PoolTransaction(tx, binary_tx, my_fee, 0)))
		.second)
		all_inserted = false;
	if (!m_memory_state_fee_tx[my_fee_per_byte].insert(tid).second)
		all_inserted = false;
	invariant(all_inserted, "memory_state_fee_tx empty");
	m_memory_state_total_size += my_size;
	while (m_memory_state_total_size > MAX_POOL_SIZE) {
		invariant(!m_memory_state_fee_tx.empty(), "memory_state_fee_tx empty");
		auto &be = m_memory_state_fee_tx.begin()->second;
		invariant(!be.empty(), "memory_state_fee_tx empty set");
		Hash rhash = *(be.begin());
		const PoolTransaction &minimal_tx = m_memory_state_tx.at(rhash);
		if (m_memory_state_total_size < MAX_POOL_SIZE + minimal_tx.binary_tx.size())
			break;  // Removing would diminish pool below max size
		remove_from_pool(rhash);
	}
	auto min_size = m_memory_state_fee_tx.empty() || m_memory_state_fee_tx.begin()->second.empty()
		? 0
		: m_memory_state_tx.at(*(m_memory_state_fee_tx.begin()->second.begin())).binary_tx.size();
	auto min_fee_per_byte = m_memory_state_fee_tx.empty() || m_memory_state_fee_tx.begin()->second.empty()
		? 0
		: m_memory_state_fee_tx.begin()->first;

	m_log(logging::INFO) << "Added transaction with hash=" << tid << " size=" << my_size << " fee=" << my_fee
		<< " fee/byte=" << my_fee_per_byte << " current_pool_size=("
		<< m_memory_state_total_size - min_size << "+" << min_size << ")=" << m_memory_state_total_size
		<< " count=" << m_memory_state_tx.size() << " min fee/byte=" << min_fee_per_byte << std::endl;

	m_tx_pool_version += 1;
	return AddTransactionResult::BROADCAST_ALL;
}

bool BlockChainState::get_largest_referenced_height(const TransactionPrefix &transaction, Height *block_height) const {
	std::map<Amount, uint32_t> largest_indices;  // Do not check same used amount twice
	size_t input_index = 0;
	for (const auto &input : transaction.inputs) {
		if (input.type() == typeid(KeyInput)) {
			const KeyInput &in = boost::get<KeyInput>(input);
			if (in.output_indexes.empty())
				return false;  // semantics invalid
			uint32_t largest_index = in.output_indexes[0];
			for (size_t i = 1; i < in.output_indexes.size(); ++i) {
				largest_index = largest_index + in.output_indexes[i];
			}
			auto &lit = largest_indices[in.amount];
			if (largest_index > lit)
				lit = largest_index;
		}
		input_index++;
	}
	Height max_height = 0;
	for (auto lit : largest_indices) {
		UnlockTimePublickKeyHeightSpent unp;
		if (!read_amount_output(lit.first, lit.second, &unp))
			return false;
		max_height = std::max(max_height, unp.height);
	}
	*block_height = max_height;
	return true;
}

void BlockChainState::remove_from_pool(Hash tid) {
	auto tit = m_memory_state_tx.find(tid);
	if (tit == m_memory_state_tx.end())
		return;
	bool all_erased = true;
	const Transaction &tx = tit->second.tx;
	for (const auto &input : tx.inputs) {
		if (input.type() == typeid(KeyInput)) {
			const KeyInput &in = boost::get<KeyInput>(input);
			if (m_memory_state_ki_tx.erase(in.key_image) != 1)
				all_erased = false;
		}
	}
	const size_t my_size = tit->second.binary_tx.size();
	const Amount my_fee_per_byte = tit->second.fee_per_byte();
	if (m_memory_state_fee_tx[my_fee_per_byte].erase(tid) != 1)
		all_erased = false;
	if (m_memory_state_fee_tx[my_fee_per_byte].empty())
		m_memory_state_fee_tx.erase(my_fee_per_byte);
	m_memory_state_total_size -= my_size;
	m_memory_state_tx.erase(tit);
	invariant(all_erased, "remove_memory_pool failed to erase everything");

	auto min_size = m_memory_state_fee_tx.empty() || m_memory_state_fee_tx.begin()->second.empty()
		? 0
		: m_memory_state_tx.at(*(m_memory_state_fee_tx.begin()->second.begin())).binary_tx.size();
	auto min_fee_per_byte = m_memory_state_fee_tx.empty() || m_memory_state_fee_tx.begin()->second.empty()
		? 0
		: m_memory_state_fee_tx.begin()->first;
	m_log(logging::INFO) << "Removed transaction with hash=" << tid << " size=" << my_size << " current_pool_size=("
		<< m_memory_state_total_size - min_size << "+" << min_size << ")=" << m_memory_state_total_size
		<< " count=" << m_memory_state_tx.size() << " min fee/byte=" << min_fee_per_byte << std::endl;
}


std::string BlockChainState::redo_transaction_get_error(bool generating, const Transaction &transaction,
	DeltaState *delta_state, BlockGlobalIndices *global_indices, Height *conflict_height, bool check_sigs) const {
	const bool check_outputs = check_sigs;
	Hash tx_prefix_hash;
	if (check_sigs)
		tx_prefix_hash = get_transaction_prefix_hash(transaction);
	DeltaState tx_delta(delta_state->get_block_height(), delta_state->get_unlock_timestamp(), delta_state);
	global_indices->resize(global_indices->size() + 1);
	auto &my_indices = global_indices->back();
	my_indices.reserve(transaction.outputs.size());

	*conflict_height = 0;
	size_t input_index = 0;
	for (const auto &input : transaction.inputs) {
		if (input.type() == typeid(KeyInput)) {
			const KeyInput &in = boost::get<KeyInput>(input);

			if (check_sigs || check_outputs) {
				Height height = 0;
				if (tx_delta.read_keyimage(in.key_image, &height)) {
					*conflict_height = height;
					return "INPUT_KEYIMAGE_ALREADY_SPENT";
				}
				if (in.output_indexes.empty())
					return "INPUT_UNKNOWN_TYPE";  // Never - checked in validate_semantic
				std::vector<uint32_t> global_indexes(in.output_indexes.size());
				global_indexes[0] = in.output_indexes[0];
				for (size_t i = 1; i < in.output_indexes.size(); ++i) {
					global_indexes[i] = global_indexes[i - 1] + in.output_indexes[i];
				}
				std::vector<PublicKey> output_keys(global_indexes.size());
				for (size_t i = 0; i != global_indexes.size(); ++i) {
					UnlockTimePublickKeyHeightSpent unp;
					if (!tx_delta.read_amount_output(in.amount, global_indexes[i], &unp)) {
						*conflict_height = m_currency.max_block_height;
						return "INPUT_INVALID_GLOBAL_INDEX";
					}
					*conflict_height = std::max(*conflict_height, unp.height);
					if (!m_currency.is_transaction_spend_time_unlocked(
						unp.unlock_time, delta_state->get_block_height(), delta_state->get_unlock_timestamp()))
						return "INPUT_SPEND_LOCKED_OUT";
					output_keys[i] = unp.public_key;
				}
				std::vector<const PublicKey *> output_key_pointers;
				output_key_pointers.reserve(output_keys.size());
				std::for_each(output_keys.begin(), output_keys.end(),
					[&output_key_pointers](const PublicKey &key) { output_key_pointers.push_back(&key); });
				bool key_corrupted = false;
				if (check_sigs &&
					!check_ring_signature(tx_prefix_hash, in.key_image, output_key_pointers.data(),
						output_key_pointers.size(), transaction.signatures[input_index].data(), true, &key_corrupted)) {
					if (key_corrupted)  // TODO - db corrupted
						return "INPUT_CORRUPTED_SIGNATURES";
					return "INPUT_INVALID_SIGNATURES";
				}
			}
			if (in.output_indexes.size() == 1)
				tx_delta.spend_output(in.amount, in.output_indexes[0]);
			tx_delta.store_keyimage(in.key_image, delta_state->get_block_height());
		}
		input_index++;
	}
	for (const auto &output : transaction.outputs) {
		if (output.target.type() == typeid(KeyOutput)) {
			const KeyOutput &key_output = boost::get<KeyOutput>(output.target);
			auto global_index = tx_delta.push_amount_output(output.amount, transaction.unlock_time, 0,
				key_output.key);
			my_indices.push_back(global_index);
		}
	}
	tx_delta.apply(delta_state);
	return std::string();
}

void BlockChainState::undo_transaction(IBlockChainState *delta_state, Height, const Transaction &tx) {
	for (auto oit = tx.outputs.rbegin(); oit != tx.outputs.rend(); ++oit) {
		if (oit->target.type() == typeid(KeyOutput)) {
			delta_state->pop_amount_output(oit->amount, tx.unlock_time, boost::get<KeyOutput>(oit->target).key);
		}
	}
	for (auto iit = tx.inputs.rbegin(); iit != tx.inputs.rend(); ++iit) {
		if (iit->type() == typeid(KeyInput)) {
			const KeyInput &in = boost::get<KeyInput>(*iit);
			delta_state->delete_keyimage(in.key_image);
			if (in.output_indexes.size() == 1)
				spend_output(in.amount, in.output_indexes[0], false);
		}
	}
}

bool BlockChainState::redo_block(const Block &block,
	const api::BlockHeader &info,
	DeltaState *delta_state,
	BlockGlobalIndices *global_indices) const {
	Height conflict_height;
	std::string result = redo_transaction_get_error(
		true, block.header.base_transaction, delta_state, global_indices, &conflict_height, false);
	if (!result.empty())
		return false;
	for (auto tit = block.transactions.begin(); tit != block.transactions.end(); ++tit) {
		std::string result =
			redo_transaction_get_error(false, *tit, delta_state, global_indices, &conflict_height, false);
		if (!result.empty())
			return false;
	}
	return true;
}

bool BlockChainState::redo_block(const Hash &bhash, const Block &block, const api::BlockHeader &info) {
	DeltaState delta(info.height, info.timestamp, this);
	BlockGlobalIndices global_indices;
	global_indices.reserve(block.transactions.size() + 1);
	const bool check_sigs = !m_currency.is_in_sw_checkpoint_zone(info.height + 1);
	if (check_sigs && !ring_checker.start_work_get_error(this, m_currency, block, info.height, info.timestamp).empty())
		return false;
	if (!redo_block(block, info, &delta, &global_indices))
		return false;
	if (check_sigs && !ring_checker.signatures_valid())
		return false;
	delta.apply(this);
	m_tx_pool_version = 2;

	auto key =
		BLOCK_GLOBAL_INDICES_PREFIX + DB::to_binary_key(bhash.data, sizeof(bhash.data)) + BLOCK_GLOBAL_INDICES_SUFFIX;
	BinaryArray ba = seria::to_binary(global_indices);
	m_db.put(key, ba, true);


	auto now = std::chrono::steady_clock::now();
	if (m_config.is_testnet || std::chrono::duration_cast<std::chrono::milliseconds>(now - log_redo_block_timestamp).count() > 1000) {
		log_redo_block_timestamp = now;
		m_log(logging::INFO) << "redo_block height=" << info.height << " bid=" << bhash
			<< " #tx=" << block.transactions.size() << std::endl;
	}
	else {
		if (check_sigs)
			m_log(logging::TRACE) << "redo_block height=" << info.height << " bid=" << bhash
			<< " #tx=" << block.transactions.size() << std::endl;
	}
	return true;
}

void BlockChainState::undo_block(const Hash &bhash, const Block &block, Height height) {

	m_log(logging::INFO) << "undo_block height=" << height << " bid=" << bhash
		<< " new tip_bid=" << block.header.previous_block_hash << std::endl;
	for (auto tit = block.transactions.rbegin(); tit != block.transactions.rend(); ++tit) {
		undo_transaction(this, height, *tit);
	}
	undo_transaction(this, height, block.header.base_transaction);

	auto key =
		BLOCK_GLOBAL_INDICES_PREFIX + DB::to_binary_key(bhash.data, sizeof(bhash.data)) + BLOCK_GLOBAL_INDICES_SUFFIX;
	m_db.del(key, true);
}

bool BlockChainState::read_block_output_global_indices(const Hash &bid, BlockGlobalIndices *indices) const {
	BinaryArray rb;
	auto key =
		BLOCK_GLOBAL_INDICES_PREFIX + DB::to_binary_key(bid.data, sizeof(bid.data)) + BLOCK_GLOBAL_INDICES_SUFFIX;
	if (!m_db.get(key, rb))
		return false;
	seria::from_binary(*indices, rb);
	return true;
}

std::vector<api::Output> BlockChainState::get_random_outputs(
	Amount amount, size_t outs_count, Height height, Timestamp time) const {
	std::vector<api::Output> result;
	uint32_t total_count = next_global_index_for_amount(amount);
	if (total_count <= outs_count) {
		for (uint32_t i = 0; i != total_count; ++i) {
			api::Output item;
			UnlockTimePublickKeyHeightSpent unp;
			item.amount = amount;
			item.global_index = i;
			invariant(read_amount_output(amount, i, &unp), "global amount < total_count not found");
			item.unlock_time = unp.unlock_time;
			item.public_key = unp.public_key;
			item.height = unp.height;
			if (unp.spent || unp.height > height)
				continue;
			if (!m_currency.is_transaction_spend_time_unlocked(item.unlock_time, height, time))
				continue;
			result.push_back(item);
		}
		return result;
	}
	std::set<uint32_t> tried_or_added;
	crypto::random_engine<uint64_t> generator;
	std::lognormal_distribution<double> distribution(1.9, 1.0);
	size_t attempts = 0;
	for (; result.size() < outs_count && attempts < outs_count * 20; ++attempts) {  // TODO - 20

		double sample = distribution(generator);
		int d_num = static_cast<int>(std::floor(total_count * (1 - std::pow(10, -sample / 10))));
		if (d_num < 0 || d_num >= int(total_count))
			continue;
		uint32_t num = static_cast<uint32_t>(d_num);
		if (!tried_or_added.insert(num).second)
			continue;
		api::Output item;
		UnlockTimePublickKeyHeightSpent unp;
		item.amount = amount;
		item.global_index = num;
		invariant(read_amount_output(amount, num, &unp), "num < total_count not found");
		item.unlock_time = unp.unlock_time;
		item.public_key = unp.public_key;
		item.height = unp.height;
		if (unp.spent || unp.height > height)
			continue;
		if (!m_currency.is_transaction_spend_time_unlocked(item.unlock_time, height, time))
			continue;
		result.push_back(item);
	}
	return result;
}

void BlockChainState::store_keyimage(const KeyImage &key_image, Height height) {
	auto key = KEYIMAGE_PREFIX + DB::to_binary_key(key_image.data, sizeof(key_image.data));
	m_db.put(key, seria::to_binary(height), true);
	auto tit = m_memory_state_ki_tx.find(key_image);
	if (tit == m_memory_state_ki_tx.end())
		return;
	remove_from_pool(tit->second);
}

void BlockChainState::delete_keyimage(const KeyImage &key_image) {
	auto key = KEYIMAGE_PREFIX + DB::to_binary_key(key_image.data, sizeof(key_image.data));
	m_db.del(key, true);
}

bool BlockChainState::read_keyimage(const KeyImage &key_image, Height *height) const {
	auto key = KEYIMAGE_PREFIX + DB::to_binary_key(key_image.data, sizeof(key_image.data));
	BinaryArray rb;
	if (!m_db.get(key, rb))
		return false;
	seria::from_binary(*height, rb);
	return true;
}

uint32_t BlockChainState::push_amount_output(Amount amount, UnlockMoment unlock_time, Height block_height, const PublicKey &pk) {
	uint32_t my_gi = next_global_index_for_amount(amount);
	auto key = AMOUNT_OUTPUT_PREFIX + common::write_varint_sqlite4(amount) + common::write_varint_sqlite4(my_gi);
	BinaryArray ba = seria::to_binary(UnlockTimePublickKeyHeightSpent{ unlock_time, pk, block_height });
	m_db.put(key, ba, true);
	m_next_gi_for_amount[amount] += 1;
	return my_gi;
}

void BlockChainState::pop_amount_output(Amount amount, UnlockMoment unlock_time, const PublicKey &pk) {
	uint32_t next_gi = next_global_index_for_amount(amount);
	invariant(next_gi != 0, "BlockChainState::pop_amount_output underflow");
	next_gi -= 1;
	m_next_gi_for_amount[amount] -= 1;
	auto key = AMOUNT_OUTPUT_PREFIX + common::write_varint_sqlite4(amount) + common::write_varint_sqlite4(next_gi);

	UnlockTimePublickKeyHeightSpent unp;
	invariant(read_amount_output(amount, next_gi, &unp), "BlockChainState::pop_amount_output element does not exist");
	// TODO - check also was_height after upgrade to version 4 ?
	invariant(!unp.spent && unp.unlock_time == unlock_time && unp.public_key == pk,
		"BlockChainState::pop_amount_output popping wrong element");
	m_db.del(key, true);
}

uint32_t BlockChainState::next_global_index_for_amount(Amount amount) const {
	auto it = m_next_gi_for_amount.find(amount);
	if (it != m_next_gi_for_amount.end())
		return it->second;
	std::string prefix = AMOUNT_OUTPUT_PREFIX + common::write_varint_sqlite4(amount);
	DB::Cursor cur2 = m_db.rbegin(prefix);
	uint32_t alt_in = cur2.end() ? 0 : boost::lexical_cast<Height>(common::read_varint_sqlite4(cur2.get_suffix())) + 1;
	m_next_gi_for_amount[amount] = alt_in;
	return alt_in;
}

bool BlockChainState::read_amount_output(
	Amount amount, uint32_t global_index, UnlockTimePublickKeyHeightSpent *unp) const {
	auto key = AMOUNT_OUTPUT_PREFIX + common::write_varint_sqlite4(amount) + common::write_varint_sqlite4(global_index);
	BinaryArray rb;
	if (!m_db.get(key, rb))
		return false;
	seria::from_binary(*unp, rb);
	return true;
}

void BlockChainState::spend_output(Amount amount, uint32_t global_index) { spend_output(amount, global_index, true); }
void BlockChainState::spend_output(Amount amount, uint32_t global_index, bool spent) {
	auto key = AMOUNT_OUTPUT_PREFIX + common::write_varint_sqlite4(amount) + common::write_varint_sqlite4(global_index);
	BinaryArray rb;
	if (!m_db.get(key, rb))
		return;
	UnlockTimePublickKeyHeightSpent was;
	seria::from_binary(was, rb);
	was.spent = spent;
	m_db.put(key, seria::to_binary(was), false);
}

void BlockChainState::test_print_outputs() {
	Amount previous_amount = (Amount)-1;
	uint32_t next_global_index = 0;
	int total_counter = 0;
	std::map<Amount, uint32_t> coins;
	for (DB::Cursor cur = m_db.begin(AMOUNT_OUTPUT_PREFIX); !cur.end(); cur.next()) {
		const char *be = cur.get_suffix().data();
		const char *en = be + cur.get_suffix().size();
		auto amount = common::read_varint_sqlite4(be, en);
		uint32_t global_index = boost::lexical_cast<uint32_t>(common::read_varint_sqlite4(be, en));
		if (be != en)
			std::cout << "Excess value bytes for amount=" << amount << " global_index=" << global_index << std::endl;
		if (amount != previous_amount) {
			if (previous_amount != (Amount)-1) {
				if (!coins.insert(std::make_pair(previous_amount, next_global_index)).second) {
					std::cout << "Duplicate amount for previous_amount=" << previous_amount
						<< " next_global_index=" << next_global_index << std::endl;
				}
			}
			previous_amount = amount;
			next_global_index = 0;
		}
		if (global_index != next_global_index) {
			std::cout << "Bad output index for amount=" << amount << " global_index=" << global_index << std::endl;
		}
		next_global_index += 1;
		if (++total_counter % 2000000 == 0)
			std::cout << "Working on amount=" << amount << " global_index=" << global_index << std::endl;
	}
	total_counter = 0;
	std::cout << "Total coins=" << total_counter << " total stacks=" << coins.size() << std::endl;
	for (auto &&co : coins) {
		auto total_count = next_global_index_for_amount(co.first);
		if (total_count != co.second)
			std::cout << "Wrong next_global_index_for_amount amount=" << co.first << " total_count=" << total_count
			<< " should be " << co.second << std::endl;
		for (uint32_t i = 0; i != total_count; ++i) {
			UnlockTimePublickKeyHeightSpent unp;
			if (!read_amount_output(co.first, i, &unp))
				std::cout << "Failed to read amount=" << co.first << " global_index=" << i << std::endl;
			if (++total_counter % 1000000 == 0)
				std::cout << "Working on amount=" << co.first << " global_index=" << i << std::endl;
		}
	}
}
