// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include "WalletStateBasic.hpp"
#include "Config.hpp"
#include "CryptoNoteTools.hpp"
#include "TransactionBuilder.hpp"
#include "TransactionExtra.hpp"
#include "common/Varint.hpp"
#include "common/string.hpp"
#include "crypto/crypto.hpp"
#include "platform/PathTools.hpp"
#include "seria/BinaryInputStream.hpp"
#include "seria/KVBinaryInputStream.hpp"
#include "seria/KVBinaryOutputStream.hpp"

static const auto LEVEL = logging::TRACE;

static const std::string version_current = "3";

static const std::string INDEX_UID_to_STATE = "X";

static const std::string INDEX_HEIGHT_to_HEADER = "c";


static const std::string INDEX_TID_to_TRANSACTIONS = "tx";  


static const std::string INDEX_ADDRESS_HEIGHT_TID = "th"; 


static const std::string INDEX_ADDRESS_to_BALANCE = "ba"; 


static const std::string INDEX_KEYIMAGE_to_HE_AM_GI = "ki";  


static const std::string INDEX_HE_AM_GI_to_OUTPUT = "un";
static const std::string INDEX_ADDRESS_HE_AM_GI   = "uh";


static const std::string UNLOCKED_INDEX_REALHE_AM_GI_to_OUTPUT =   "ul"; 


static const std::string LOCKED_INDEX_KI_AM_GI = "li";


static const std::string LOCKED_INDEX_HEIGHT_AM_GI_to_OUTPUT    = "lh";  
static const std::string LOCKED_INDEX_TIMESTAMP_AM_GI_to_OUTPUT = "lt";  

using namespace cryonerocoin;
using namespace platform;

void seria::ser_members(cryonerocoin::WalletStateBasic::HeightAmounGi &v, ISeria &s) {
	seria_kv("height", v.height, s);
	seria_kv("amount", v.amount, s);
	seria_kv("global_index", v.global_index, s);
}

void seria::ser_members(cryonerocoin::WalletStateBasic::UndoValue &v, seria::ISeria &s) {
	seria_kv("exists", v.exists, s);
	seria_kv("value", v.value, s);
}



WalletStateBasic::WalletStateBasic(
    logging::ILogger &log, const Config &config, const Currency &currency, const std::string &cache_name)
    : m_genesis_bid(currency.genesis_block_hash)
    , m_config(config)
    , m_currency(currency)
    , m_log(log, "WalletState")
    , m_db(false, config.get_data_folder("wallet_cache") + "/" + cache_name, 0x2000000000)  // 128 gb
{
	std::string version;
	std::string other_genesis_bid;
	std::string other_cache_name;
	m_db.get("$version", version);
	m_db.get("$genesis_bid", other_genesis_bid);
	m_db.get("$cache_name", other_cache_name);
	if (version != version_current || other_genesis_bid != common::pod_to_hex(m_genesis_bid) ||
	    other_cache_name != cache_name) {
		m_log(logging::INFO) << "Data format, wallet seed or genesis bid different, old version=" << version
		                     << " current version=" << version_current << ", clearing wallet cache..." << std::endl;
		auto total_items = m_db.get_approximate_items_count();
		size_t erased      = 0;
		for (auto cur = m_db.rbegin(std::string()); !cur.end(); cur.erase()) {
			if (erased % 1000000 == 0)
				m_log(logging::INFO) << "Processing " << erased / 1000000 << "/" << (total_items + 999999) / 1000000
				                     << " million DB records" << std::endl;
			erased += 1;
		}
		m_db.put("$version", version_current, true);
		m_db.put("$cache_name", cache_name, true);
		m_db.put("$genesis_bid", common::pod_to_hex(m_genesis_bid), true);
	}
	{  
		auto cur1 = m_db.begin(INDEX_HEIGHT_to_HEADER);
		auto cur2 = m_db.rbegin(INDEX_HEIGHT_to_HEADER);
		if (!cur1.end() && !cur2.end()) {
			m_tip_height = boost::lexical_cast<Height>(common::read_varint_sqlite4(cur2.get_suffix()));
			;
			m_tail_height = boost::lexical_cast<Height>(common::read_varint_sqlite4(cur1.get_suffix()));
			m_tip         = (m_tip_height + 1 == m_tail_height) ? api::BlockHeader{} : read_chain(m_tip_height);
		}
		fix_empty_chain();
	}
}

void WalletStateBasic::combine_balance(
    api::Balance &balance, const api::Output &output, int locked_op, int spendable_op) {
	common::Uint128 &mod = output.dust ? balance.spendable_dust : balance.spendable;
	uint64_t &mod_coins  = output.dust ? balance.spendable_dust_outputs : balance.spendable_outputs;
	if (locked_op > 0)
		balance.locked_or_unconfirmed += output.amount;
	if (locked_op < 0)
		balance.locked_or_unconfirmed -= output.amount;
	balance.locked_or_unconfirmed_outputs += locked_op;
	if (spendable_op > 0)
		mod += output.amount;
	if (spendable_op < 0)
		mod -= output.amount;
	mod_coins += spendable_op;
}

void WalletStateBasic::db_commit() {
	m_log(logging::INFO) << "WalletState::db_commit started... tip_height=" << m_tip_height << std::endl;
	m_db.commit_db_txn();
	m_log(logging::TRACE) << "WalletState::db_commit finished..." << std::endl;
}

std::string WalletStateBasic::format_output(const api::Output &v) {
	std::stringstream str;
	str << " he=" << v.height << " am=" << m_currency.format_amount(v.amount) << " gi=" << v.global_index
	    << " ki=" << v.key_image << " addr=" << v.address
	    << (v.unlock_time == 0 ? "" : " unl=" + common::to_string(v.unlock_time));
	return str.str();
}

void WalletStateBasic::push_chain(const api::BlockHeader &header) {
	m_tip_height += 1;
	BinaryArray ba = seria::to_binary(header);
	m_db.put(INDEX_HEIGHT_to_HEADER + common::write_varint_sqlite4(m_tip_height), ba, true);
	m_tip = header;

	save_db_state(m_tip_height, current_undo_map);
	current_undo_map.clear();
}
void WalletStateBasic::pop_chain() {
	invariant(m_tip_height + 1 != m_tail_height, "pop_chain tip_height == -1");
	undo_db_state(m_tip_height);
	m_db.del(INDEX_HEIGHT_to_HEADER + common::write_varint_sqlite4(m_tip_height), true);
	m_tip_height -= 1;
	m_tip = (m_tip_height + 1 == m_tail_height) ? api::BlockHeader{} : read_chain(m_tip_height);
}

void WalletStateBasic::fix_empty_chain() {
	if (m_tip_height + 1 == m_tail_height) {
		m_tail_height = 0;
		m_tip_height  = -1;
		push_chain(BlockChainState::fill_genesis(m_genesis_bid, m_currency.genesis_block_template));
	}
}

void WalletStateBasic::reset_chain(Height new_tail_height) {
	invariant(empty_chain(), "reset_chain chain should be empty");
	m_tail_height = new_tail_height;
	m_tip_height  = m_tail_height - 1;
}

bool WalletStateBasic::read_chain(uint32_t height, api::BlockHeader &header) const {
	BinaryArray rb;
	if (!m_db.get(INDEX_HEIGHT_to_HEADER + common::write_varint_sqlite4(height), rb))
		return false;
	seria::from_binary(header, rb);
	return true;
}

api::BlockHeader WalletStateBasic::read_chain(uint32_t height) const {
	api::BlockHeader ha;
	invariant(read_chain(height, ha), "read_header_chain failed");
	return ha;
}

std::vector<Hash> WalletStateBasic::get_sparse_chain() const {
	std::vector<Hash> tip_path;

	uint32_t jump = 0;
	if (m_tip_height + 1 > m_tail_height)
		while (m_tip_height >= jump + m_tail_height) {
			tip_path.push_back(read_chain(m_tip_height - jump).hash);
			if (tip_path.size() <= 10)
				jump += 1;
			else
				jump += (1 << (tip_path.size() - 10));
		}
	if (tip_path.empty() || tip_path.back() != m_genesis_bid)
		tip_path.push_back(m_genesis_bid);
	return tip_path;
}

void WalletStateBasic::record_undo(UndoMap &undo_map, const std::string &key) {
	UndoMap::iterator kit = undo_map.find(key);
	if (kit == undo_map.end()) {
		kit = undo_map.insert(std::make_pair(key, UndoValue{})).first;
		common::BinaryArray was_value;
		if (m_db.get(key, was_value)) {
			kit->second.exists = true;
			kit->second.value  = std::move(was_value);
		}
	}
}

void WalletStateBasic::put_with_undo(const std::string &key, const common::BinaryArray &value, bool nooverwrite) {
	record_undo(current_undo_map, key);
	m_db.put(key, value, nooverwrite);
}

void WalletStateBasic::del_with_undo(const std::string &key, bool mustexist) {
	record_undo(current_undo_map, key);
	m_db.del(key, mustexist);
}

void WalletStateBasic::save_db_state(uint32_t state, const UndoMap &undo_map) {
	if (undo_map.empty())
		return;
	const auto key            = INDEX_UID_to_STATE + common::write_varint_sqlite4(state);
	common::BinaryArray value = seria::to_binary(undo_map);
	m_db.put(key, value, true);
}
void WalletStateBasic::undo_db_state(uint32_t state) {
	const auto key = INDEX_UID_to_STATE + common::write_varint_sqlite4(state);
	common::BinaryArray value;
	if (!m_db.get(key, value))
		return;
	UndoMap undo_map;
	seria::from_binary(undo_map, value);
	m_db.del(key, true);
	for (auto &&uv : undo_map) {
		if (uv.second.exists)
			m_db.put(uv.first, uv.second.value, false);
		else
			m_db.del(uv.first, false);
	}
}
bool WalletStateBasic::try_add_incoming_output(const api::Output &output, Amount *confirmed_balance_delta) const {
	HeightAmounGi heamgi;
	bool ki_exists = read_by_keyimage(output.key_image, &heamgi);
	api::Output existing_output;
	bool is_existing_unspent = ki_exists && read_from_unspent_index(heamgi, &existing_output);
	if (ki_exists && !is_existing_unspent)
		return false;
	if (output.unlock_time != 0) {
		*confirmed_balance_delta = output.amount;  
		return true;
	}
	*confirmed_balance_delta = output.amount;
	if (ki_exists) { 
		if (output.amount <= heamgi.amount ||
		    output.address != existing_output.address)  
			return false;
		*confirmed_balance_delta = output.amount - heamgi.amount;
	}
	return true;
}

Amount WalletStateBasic::add_incoming_output(const api::Output &output, bool just_unlocked) {
	HeightAmounGi heamgi;
	bool ki_exists = read_by_keyimage(output.key_image, &heamgi);
	api::Output existing_output;
	bool is_existing_unspent = ki_exists && read_from_unspent_index(heamgi, &existing_output);
	if (ki_exists && !is_existing_unspent) {
		m_log(logging::WARNING) << "  Duplicate key_output attack, ignoring output because already spent" << std::endl;
		return 0;
	}
	if (output.unlock_time != 0 && !just_unlocked) {  
		add_to_lock_index(output);
		return output.amount;
	}
	Amount added_amount = output.amount;
	if (ki_exists) {  
		if (output.amount <= heamgi.amount || output.address != existing_output.address) {
			m_log(logging::WARNING)
			    << "  Duplicate key_output attack, ignoring output because have another one unspent with same or larger amount or different address, "
			    << format_output(existing_output) << std::endl;
			return 0;
		}
		added_amount = output.amount - heamgi.amount;
		m_log(logging::WARNING)
		    << "  Duplicate key_output attack, reducing amount because have another one unspent with smaller amount, "
		    << format_output(existing_output) << std::endl;
		remove_from_unspent_index(existing_output);
	}
	add_to_unspent_index(output);
	heamgi.height       = output.height;
	heamgi.amount       = output.amount;
	heamgi.global_index = output.global_index;
	update_keyimage(output.key_image, heamgi, !ki_exists);
	return added_amount;
}
Amount WalletStateBasic::add_incoming_output(const api::Output &output) {
	m_log(LEVEL) << "Incoming output " << format_output(output) << std::endl;
	return add_incoming_output(output, false);
}

Amount WalletStateBasic::add_incoming_keyimage(Height block_height, const KeyImage &key_image) {
	m_log(LEVEL) << "Incoming keyimage " << key_image << std::endl;
	std::string prefix = LOCKED_INDEX_KI_AM_GI + DB::to_binary_key(key_image.data, sizeof(key_image.data));
	std::vector<api::Output> found_in_locked;
	for (auto cur = m_db.begin(prefix); !cur.end(); cur.next()) {
		const std::string &suf = cur.get_suffix();
		const char *be         = suf.data();
		const char *en         = be + suf.size();
		Amount am              = common::read_varint_sqlite4(be, en);
		uint32_t gi            = boost::lexical_cast<uint32_t>(common::read_varint_sqlite4(be, en));
		invariant(en - be == 0, "");
		UnlockMoment unl = 0;
		seria::from_binary(unl, cur.get_value_array());
		uint32_t clamped_unlock_time = static_cast<uint32_t>(std::min<UnlockMoment>(unl, 0xFFFFFFFF));
		std::string unkey = m_currency.is_transaction_spend_time_block(unl) ? LOCKED_INDEX_HEIGHT_AM_GI_to_OUTPUT
		                                                                    : LOCKED_INDEX_TIMESTAMP_AM_GI_to_OUTPUT;
		unkey += common::write_varint_sqlite4(clamped_unlock_time) + common::write_varint_sqlite4(am) +
		         common::write_varint_sqlite4(gi);
		BinaryArray output_ba;
		invariant(m_db.get(unkey, output_ba), "");
		api::Output output;
		seria::from_binary(output, output_ba);
		found_in_locked.push_back(output);
	}
	for (auto &&lo : found_in_locked) {
		unlock(block_height, std::move(lo));
	}
	Amount removed_amount = 0;
	HeightAmounGi heamgi;
	bool ki_exists = read_by_keyimage(key_image, &heamgi);
	api::Output existing_output;
	if (ki_exists && read_from_unspent_index(heamgi, &existing_output)) {
		removed_amount = existing_output.amount;
		remove_from_unspent_index(existing_output);
	}
	return removed_amount;
}

bool WalletStateBasic::try_adding_incoming_keyimage(const KeyImage &key_image, api::Output *spending_output) const {
	bool candidate_found = false;
	HeightAmounGi heamgi;
	bool ki_exists = read_by_keyimage(key_image, &heamgi);
	if (ki_exists && read_from_unspent_index(heamgi, spending_output)) {
		candidate_found = true;
	}
	std::string prefix = LOCKED_INDEX_KI_AM_GI + DB::to_binary_key(key_image.data, sizeof(key_image.data));
	for (auto cur = m_db.begin(prefix); !cur.end(); cur.next()) {
		const std::string &suf = cur.get_suffix();
		const char *be         = suf.data();
		const char *en         = be + suf.size();
		Amount am              = common::read_varint_sqlite4(be, en);
		uint32_t gi            = boost::lexical_cast<uint32_t>(common::read_varint_sqlite4(be, en));
		invariant(en - be == 0, "");
		if (candidate_found && am <= spending_output->amount)
			continue;
		UnlockMoment unl = 0;
		seria::from_binary(unl, cur.get_value_array());
		uint32_t clamped_unlock_time = static_cast<uint32_t>(std::min<UnlockMoment>(unl, 0xFFFFFFFF));
		std::string unkey = m_currency.is_transaction_spend_time_block(unl) ? LOCKED_INDEX_HEIGHT_AM_GI_to_OUTPUT
		                                                                    : LOCKED_INDEX_TIMESTAMP_AM_GI_to_OUTPUT;
		unkey += common::write_varint_sqlite4(clamped_unlock_time) + common::write_varint_sqlite4(am) +
		         common::write_varint_sqlite4(gi);
		BinaryArray output_ba;
		invariant(m_db.get(unkey, output_ba), "");
		api::Output output;
		seria::from_binary(output, output_ba);
		invariant(output.amount == am && output.global_index == gi, "");
		if (candidate_found && output.address != spending_output->address)
			continue;
		*spending_output = output;
		candidate_found  = true;
	}
	return candidate_found;
}

void WalletStateBasic::add_transaction(Height height, const Hash &tid, const TransactionPrefix &tx, const api::Transaction &ptx) {
	auto cur = m_db.begin(INDEX_TID_to_TRANSACTIONS);
	if (cur.end())
		on_first_transaction_found(ptx.timestamp);
	auto trkey         = INDEX_TID_to_TRANSACTIONS + DB::to_binary_key(tid.data, sizeof(tid.data));
	BinaryArray str_pa = seria::to_binary(std::make_pair(tx, ptx));
	put_with_undo(trkey, str_pa, true);
	std::set<std::string> addresses;
	addresses.insert(std::string());
	for (auto &&transfer : ptx.transfers) {
		addresses.insert(transfer.address);
	}
	for (auto &&addr : addresses) {
		const std::string adtrkey = INDEX_ADDRESS_HEIGHT_TID + addr + "/" + common::write_varint_sqlite4(height) +
		               DB::to_binary_key(tid.data, sizeof(tid.data));

		put_with_undo(adtrkey, BinaryArray(), true);
	}
}

bool WalletStateBasic::api_add_unspent(std::vector<api::Output> *result, Amount *total_amount,
    const std::string &address, Height confirmed_height, Amount max_amount) const {
	auto recently_unlocked = api_get_unlocked_outputs(address, confirmed_height, std::numeric_limits<Height>::max());
	const size_t min_count = 10000; 
	return for_each_in_unspent_index(address, Height(-1), confirmed_height, [&](const api::Output &output) -> bool {
		if (!is_memory_spent(output) &&
		    recently_unlocked.count(std::make_pair(output.amount, output.global_index)) == 0) {
			result->push_back(output);
			if (!output.dust)  
				*total_amount += output.amount;
			if (*total_amount >= max_amount && result->size() >= min_count)
				return false; 
		}
		return true;
	});
}
std::vector<api::Block> WalletStateBasic::api_get_transfers(
    const std::string &address, Height *from_height, Height *to_height, bool forward, uint32_t desired_tx_count) const {
	std::vector<api::Block> result;
	if (*from_height >= *to_height)
		return result;
	auto prefix        = INDEX_ADDRESS_HEIGHT_TID + address + "/";
	std::string middle = common::write_varint_sqlite4(forward ? *from_height + 1 : *to_height);
	api::Block current_block;
	size_t total_transactions_found = 0;
	for (auto cur = forward ? m_db.begin(prefix, middle) : m_db.rbegin(prefix, middle); !cur.end(); cur.next()) {
		const std::string &suf = cur.get_suffix();
		const char *be         = suf.data();
		const char *en         = be + suf.size();
		Height height          = boost::lexical_cast<Height>(common::read_varint_sqlite4(be, en));
		Hash tid;
		invariant(en - be == sizeof(tid.data), "CD_TIPS_PREFIX corrupted");
		DB::from_binary_key(cur.get_suffix(), cur.get_suffix().size() - sizeof(tid.data), tid.data, sizeof(tid.data));
		if (forward && height > *to_height)
			break;
		if (!forward && height <= *from_height)
			break;
		TransactionPrefix ptx;
		api::Transaction tx;
		get_transaction(tid, &ptx, &tx);
		if (current_block.header.height != height && !current_block.transactions.empty()) {
			result.push_back(std::move(current_block));
			current_block = api::Block();
			if (total_transactions_found >= desired_tx_count) {
				if (forward)
					*to_height = height - 1;
				else
					*from_height = height;
				break;
			}
		}
		if (current_block.transactions.empty()) {
			read_chain(height, current_block.header);
		}
		if (!address.empty()) {
			for (auto tit = tx.transfers.begin(); tit != tx.transfers.end();)
				if (tit->address == address)
					++tit;
				else
					tit = tx.transfers.erase(tit);
		}
		current_block.transactions.push_back(std::move(tx));
		total_transactions_found += 1;
	}
	if (!current_block.transactions.empty()) {
		result.push_back(std::move(current_block));
	}
	return result;
}

std::vector<api::Output> WalletStateBasic::api_get_locked_or_unconfirmed_unspent(const std::string &address,
    Height confirmed_height) const {
	std::vector<api::Output> result;
	for_each_in_unspent_index(
	    address, confirmed_height, std::numeric_limits<Height>::max(), [&](const api::Output &output) -> bool {
		    if (!is_memory_spent(output))
			    result.push_back(output);
		    return true;
		});
	auto recently_unlocked = api_get_unlocked_outputs(address, confirmed_height, std::numeric_limits<Height>::max());
	for (auto &&lou : recently_unlocked) {
		HeightAmounGi heamgi;
		heamgi.height       = lou.second.height;
		heamgi.amount       = lou.second.amount;
		heamgi.global_index = lou.second.global_index;
		api::Output existing_output;
		bool is_existing_unspent = read_from_unspent_index(heamgi, &existing_output);
		if (!is_existing_unspent || is_memory_spent(lou.second))
			continue;
		if (lou.second.height <= confirmed_height)
			result.push_back(lou.second);
	}
	std::map<std::pair<Amount, uint32_t>, api::Output> still_locked;
	read_unlock_index(&still_locked, LOCKED_INDEX_TIMESTAMP_AM_GI_to_OUTPUT, address, uint32_t(-1), 0xFFFFFFFF);
	read_unlock_index(&still_locked, LOCKED_INDEX_HEIGHT_AM_GI_to_OUTPUT, address, uint32_t(-1), 0xFFFFFFFF);
	for (auto &&lou : still_locked)
		if (!is_memory_spent(lou.second))
			result.push_back(std::move(lou.second));
	return result;
}


api::Balance WalletStateBasic::get_balance(const std::string &address, Height confirmed_height) const {
	auto bakey = INDEX_ADDRESS_to_BALANCE + address;
	BinaryArray ba;
	api::Balance balance;
	if (m_db.get(bakey, ba))
		seria::from_binary(balance, ba);

	for_each_in_unspent_index(
	    address, confirmed_height, std::numeric_limits<Height>::max(), [&](const api::Output &output) -> bool {
		    if (is_memory_spent(output))
			    combine_balance(balance, output, 0, -1);
		    else
			    combine_balance(balance, output, 1, -1);
		    return true;
		});

	auto recently_unlocked = api_get_unlocked_outputs(address, confirmed_height, std::numeric_limits<Height>::max());
	for (auto &&lou : recently_unlocked) {
		HeightAmounGi heamgi;
		heamgi.height       = lou.second.height;
		heamgi.amount       = lou.first.first;
		heamgi.global_index = lou.first.second;
		api::Output existing_output;
		bool is_existing_unspent = read_from_unspent_index(heamgi, &existing_output);
		if (!is_existing_unspent || is_memory_spent(lou.second))
			continue;
		if (lou.second.height <= confirmed_height)
			combine_balance(balance, existing_output, 1, -1);
	}
	for (auto &&kit : get_used_key_images()) {
		HeightAmounGi heamgi;
		bool ki_exists = read_by_keyimage(kit.first, &heamgi);
		api::Output existing_output;
		bool is_existing_unspent = ki_exists && read_from_unspent_index(heamgi, &existing_output);
		if (is_existing_unspent && existing_output.height <= confirmed_height &&
		    (address.empty() || existing_output.address == address))
			combine_balance(balance, existing_output, 0, -1);
	}


	return balance;
}

bool WalletStateBasic::has_transaction(Hash tid) const {
	auto trkey = INDEX_TID_to_TRANSACTIONS + DB::to_binary_key(tid.data, sizeof(tid.data));
	BinaryArray data;
	return m_db.get(trkey, data);
}

bool WalletStateBasic::get_transaction(Hash tid, TransactionPrefix *tx, api::Transaction *ptx) const {
	auto trkey = INDEX_TID_to_TRANSACTIONS + DB::to_binary_key(tid.data, sizeof(tid.data));
	BinaryArray data;
	if (!m_db.get(trkey, data))
		return false;
	std::pair<TransactionPrefix, api::Transaction> pa;
	seria::from_binary(pa, data);
	*tx  = std::move(pa.first);
	*ptx = std::move(pa.second);
	return true;
}

static void parse_lock_key(
    const std::string &suffix, uint32_t *clamped_unlocktime, Amount *amount, uint32_t *global_index) {
	const char *be      = suffix.data();
	const char *en      = be + suffix.size();
	*clamped_unlocktime = boost::lexical_cast<uint32_t>(common::read_varint_sqlite4(be, en));
	*amount             = common::read_varint_sqlite4(be, en);
	*global_index       = boost::lexical_cast<uint32_t>(common::read_varint_sqlite4(be, en));
	invariant(en - be == 0, "");
}

void WalletStateBasic::read_unlock_index(std::map<std::pair<Amount, uint32_t>, api::Output> *add,
    const std::string &index_prefix, const std::string &address, uint32_t begin, uint32_t end) const {
	if (begin != uint32_t(-1) && begin >= end)  
		return;
	auto middle = common::write_varint_sqlite4(begin + 1);
	for (auto cur = m_db.begin(index_prefix, middle); !cur.end(); cur.next()) {
		Height height = 0;
		Amount amount = 0;
		uint32_t global_index;
		parse_lock_key(cur.get_suffix(), &height, &amount, &global_index);
		if (height > end)
			break;
		api::Output output;
		seria::from_binary(output, cur.get_value_array());

		invariant(output.global_index == global_index, "Index corrupted");
		if (address.empty() || output.address == address)
			invariant(add->insert(std::make_pair(std::make_pair(amount, output.global_index), output)).second,
			    "Invariant dead read_unlock_index adding output twice");
	}
}

std::map<std::pair<Amount, uint32_t>, api::Output> WalletStateBasic::api_get_unlocked_outputs(
    const std::string &address,
    Height from_height,
    Height to_height) const {
	std::map<std::pair<Amount, uint32_t>, api::Output> unlocked;
	read_unlock_index(&unlocked, UNLOCKED_INDEX_REALHE_AM_GI_to_OUTPUT, address, from_height, to_height);
	return unlocked;
}

void WalletStateBasic::modify_balance(const api::Output &output, int locked_op, int spendable_op) {
	auto bakey  = INDEX_ADDRESS_to_BALANCE + output.address;
	auto bakey2 = INDEX_ADDRESS_to_BALANCE;
	BinaryArray ba;
	api::Balance balance;
	api::Balance balance2;
	if (m_db.get(bakey, ba))
		seria::from_binary(balance, ba);
	if (m_db.get(bakey2, ba))
		seria::from_binary(balance2, ba);

	combine_balance(balance, output, locked_op, spendable_op);
	combine_balance(balance2, output, locked_op, spendable_op);
	if (balance.total() == 0 && balance.total_outputs() == 0)
		del_with_undo(bakey, false);
	else
		put_with_undo(bakey, seria::to_binary(balance), false);
	if (balance2.total() == 0 && balance2.total_outputs() == 0)
		del_with_undo(bakey2, false);
	else
		put_with_undo(bakey2, seria::to_binary(balance2), false);
}
static std::map<KeyImage, int> empty_keyimages;
const std::map<KeyImage, int> &WalletStateBasic::get_used_key_images() const { return empty_keyimages; }

void WalletStateBasic::unlock(Height now_height, api::Output &&output) {
	remove_from_lock_index(output);
	Amount adjusted_amount = add_incoming_output(output, true);

	auto unkey = UNLOCKED_INDEX_REALHE_AM_GI_to_OUTPUT + common::write_varint_sqlite4(now_height) +
	             common::write_varint_sqlite4(output.amount) + common::write_varint_sqlite4(output.global_index);
	output.amount  = adjusted_amount;
	BinaryArray ba = seria::to_binary(output);
	put_with_undo(unkey, ba, true);
}

void WalletStateBasic::add_to_lock_index(const api::Output &output) {
	m_log(LEVEL) << "  Adding output to lock index, " << format_output(output) << std::endl;
	modify_balance(output, 1, 0);
	uint32_t clamped_unlock_time = static_cast<uint32_t>(std::min<UnlockMoment>(output.unlock_time, 0xFFFFFFFF));
	std::string unkey            = m_currency.is_transaction_spend_time_block(output.unlock_time)
	                        ? LOCKED_INDEX_HEIGHT_AM_GI_to_OUTPUT
	                        : LOCKED_INDEX_TIMESTAMP_AM_GI_to_OUTPUT;
	unkey += common::write_varint_sqlite4(clamped_unlock_time) + common::write_varint_sqlite4(output.amount) +
	         common::write_varint_sqlite4(output.global_index);
	put_with_undo(unkey, seria::to_binary(output), true);
	if (output.key_image != KeyImage{}) {
		unkey = LOCKED_INDEX_KI_AM_GI + DB::to_binary_key(output.key_image.data, sizeof(output.key_image.data)) +
		        common::write_varint_sqlite4(output.amount) + common::write_varint_sqlite4(output.global_index);
		BinaryArray ba = seria::to_binary(output.unlock_time);
		put_with_undo(unkey, ba, true);
	}
}

void WalletStateBasic::remove_from_lock_index(const api::Output &output) {
	m_log(LEVEL) << "  Removing output from lock index, " << format_output(output) << std::endl;
	uint32_t clamped_unlock_time = static_cast<uint32_t>(std::min<UnlockMoment>(output.unlock_time, 0xFFFFFFFF));
	std::string unkey            = m_currency.is_transaction_spend_time_block(output.unlock_time)
	                        ? LOCKED_INDEX_HEIGHT_AM_GI_to_OUTPUT
	                        : LOCKED_INDEX_TIMESTAMP_AM_GI_to_OUTPUT;
	unkey += common::write_varint_sqlite4(clamped_unlock_time) + common::write_varint_sqlite4(output.amount) +
	         common::write_varint_sqlite4(output.global_index);
	modify_balance(output, -1, 0);
	del_with_undo(unkey, true);
	if (output.key_image != KeyImage{}) {
		unkey = LOCKED_INDEX_KI_AM_GI + DB::to_binary_key(output.key_image.data, sizeof(output.key_image.data)) +
		        common::write_varint_sqlite4(output.amount) + common::write_varint_sqlite4(output.global_index);
		del_with_undo(unkey, true);
	}
}

void WalletStateBasic::unlock(Height now_height, Timestamp now) {
	std::map<std::pair<Amount, uint32_t>, api::Output> to_unlock;
	read_unlock_index(&to_unlock, LOCKED_INDEX_HEIGHT_AM_GI_to_OUTPUT, std::string(), uint32_t(-1), now_height);
	read_unlock_index(&to_unlock, LOCKED_INDEX_TIMESTAMP_AM_GI_to_OUTPUT, std::string(), uint32_t(-1), now);
	if (!to_unlock.empty())
		m_log(LEVEL) << "Unlocking for height=" << now_height << ", now=" << now << std::endl;
	for (auto &&unl : to_unlock) {
		unlock(now_height, std::move(unl.second));
	}
}

bool WalletStateBasic::read_from_unspent_index(const HeightAmounGi &value, api::Output *output) const {
	auto keyun = INDEX_HE_AM_GI_to_OUTPUT + common::write_varint_sqlite4(value.height) +
	             common::write_varint_sqlite4(value.amount) + common::write_varint_sqlite4(value.global_index);
	BinaryArray ba;
	if (!m_db.get(keyun, ba))
		return false;
	seria::from_binary(*output, ba);
	return true;
}
bool WalletStateBasic::for_each_in_unspent_index(
    const std::string &address, Height from, Height to, std::function<bool(const api::Output &)> fun) const {
	auto prefix        = address.empty() ? INDEX_HE_AM_GI_to_OUTPUT : INDEX_ADDRESS_HE_AM_GI + address + "/";
	std::string middle = common::write_varint_sqlite4(from + 1);
	for (auto cur = m_db.begin(prefix, middle); !cur.end(); cur.next()) {
		const std::string &suf = cur.get_suffix();
		const char *be         = suf.data();
		const char *en         = be + suf.size();
		Height he              = boost::lexical_cast<Height>(common::read_varint_sqlite4(be, en));
		Amount am              = common::read_varint_sqlite4(be, en);
		uint32_t gi            = boost::lexical_cast<uint32_t>(common::read_varint_sqlite4(be, en));
		invariant(en - be == 0, "");
		if (he > to)
			break;
		api::Output output;
		if (!address.empty()) {
			HeightAmounGi heamgi{he, am, gi};
			invariant(read_from_unspent_index(heamgi, &output), "unspent indexes do not match");
			invariant(output.address == address, "output is in wrong index by address");
		} else
			seria::from_binary(output, cur.get_value_array());
		if (!fun(output))
			return false;
	}
	return true;
}

void WalletStateBasic::add_to_unspent_index(const api::Output &output) {
	m_log(LEVEL) << "  Adding to unspent, " << format_output(output) << std::endl;
	modify_balance(output, 0, 1);
	auto keyun = INDEX_HE_AM_GI_to_OUTPUT + common::write_varint_sqlite4(output.height) +
	             common::write_varint_sqlite4(output.amount) + common::write_varint_sqlite4(output.global_index);
	put_with_undo(keyun, seria::to_binary(output), true);

	keyun = INDEX_ADDRESS_HE_AM_GI + output.address + "/" + common::write_varint_sqlite4(output.height) +
	        common::write_varint_sqlite4(output.amount) + common::write_varint_sqlite4(output.global_index);
	put_with_undo(keyun, BinaryArray{}, true);
}

void WalletStateBasic::remove_from_unspent_index(const api::Output &output) {
	m_log(LEVEL) << "  Removing from unspent, " << format_output(output) << std::endl;
	modify_balance(output, 0, -1);
	auto keyun = INDEX_HE_AM_GI_to_OUTPUT + common::write_varint_sqlite4(output.height) +
	             common::write_varint_sqlite4(output.amount) + common::write_varint_sqlite4(output.global_index);
	del_with_undo(keyun, true);

	keyun = INDEX_ADDRESS_HE_AM_GI + output.address + "/" + common::write_varint_sqlite4(output.height) +
	        common::write_varint_sqlite4(output.amount) + common::write_varint_sqlite4(output.global_index);
	del_with_undo(keyun, true);
}

bool WalletStateBasic::read_by_keyimage(const KeyImage &ki, HeightAmounGi *value) const {
	auto keyun = INDEX_KEYIMAGE_to_HE_AM_GI + DB::to_binary_key(ki.data, sizeof(ki.data));
	BinaryArray ba;
	if (!m_db.get(keyun, ba))
		return false;
	seria::from_binary(*value, ba);
	return true;
}
void WalletStateBasic::update_keyimage(const KeyImage &ki, const HeightAmounGi &value, bool nooverwrite) {
	if (ki == KeyImage{})
		return;
	auto keyun = INDEX_KEYIMAGE_to_HE_AM_GI + DB::to_binary_key(ki.data, sizeof(ki.data));
	put_with_undo(keyun, seria::to_binary(value), nooverwrite);
}

void WalletStateBasic::test_undo_blocks() {
	int counter = 0;
	while (!empty_chain()) {
		pop_chain();
	}
	std::cout << "---- After undo everything ---- " << std::endl;
	counter = 0;
	for (auto cur = m_db.begin(std::string()); !cur.end(); cur.next()) {
		if (cur.get_suffix().find("ad/") == 0)
			continue;
		std::cout << DB::clean_key(cur.get_suffix()) << std::endl;
		if (counter++ > 2000)
			break;
	}
}

void WalletStateBasic::test_print_everything(const std::string &str) {
	std::cout << str << " tail:tip_height=" << get_tail_height() << ":" << get_tip_height() << std::endl;
	for (auto cur = m_db.begin(std::string()); !cur.end(); cur.next()) {
		if (cur.get_suffix().find(INDEX_HEIGHT_to_HEADER) == 0)
			continue;
		if (cur.get_suffix().find(INDEX_UID_to_STATE) == 0)
			continue;
		std::cout << DB::clean_key(cur.get_suffix()) << std::endl;
	}
}
