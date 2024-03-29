// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include "Currency.hpp"
#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <cctype>
#include "CryptoNoteConfig.hpp"
#include "CryptoNoteTools.hpp"
#include "Difficulty.hpp"
#include "TransactionExtra.hpp"
#include "common/Base58.hpp"
#include "common/StringTools.hpp"
#include "common/Varint.hpp"
#include "common/string.hpp"
#include "crypto/crypto.hpp"
#include "crypto/int-util.h"
#include "platform/PathTools.hpp"
#include "platform/Time.hpp"
#include "seria/BinaryInputStream.hpp"
#include "seria/BinaryOutputStream.hpp"

using namespace common;
using namespace cryonerocoin;

const std::vector<Amount> Currency::PRETTY_AMOUNTS = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 30, 40, 50, 60, 70, 80, 90,
    100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000,
    30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000,
    900000, 1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000, 20000000,
    30000000, 40000000, 50000000, 60000000, 70000000, 80000000, 90000000, 100000000, 200000000, 300000000, 400000000,
    500000000, 600000000, 700000000, 800000000, 900000000, 1000000000, 2000000000, 3000000000, 4000000000, 5000000000,
    6000000000, 7000000000, 8000000000, 9000000000, 10000000000, 20000000000, 30000000000, 40000000000, 50000000000,
    60000000000, 70000000000, 80000000000, 90000000000, 100000000000, 200000000000, 300000000000, 400000000000,
    500000000000, 600000000000, 700000000000, 800000000000, 900000000000, 1000000000000, 2000000000000, 3000000000000,
    4000000000000, 5000000000000, 6000000000000, 7000000000000, 8000000000000, 9000000000000, 10000000000000,
    20000000000000, 30000000000000, 40000000000000, 50000000000000, 60000000000000, 70000000000000, 80000000000000,
    90000000000000, 100000000000000, 200000000000000, 300000000000000, 400000000000000, 500000000000000,
    600000000000000, 700000000000000, 800000000000000, 900000000000000, 1000000000000000, 2000000000000000,
    3000000000000000, 4000000000000000, 5000000000000000, 6000000000000000, 7000000000000000, 8000000000000000,
    9000000000000000, 10000000000000000, 20000000000000000, 30000000000000000, 40000000000000000, 50000000000000000,
    60000000000000000, 70000000000000000, 80000000000000000, 90000000000000000, 100000000000000000, 200000000000000000,
    300000000000000000, 400000000000000000, 500000000000000000, 600000000000000000, 700000000000000000,
    800000000000000000, 900000000000000000, 1000000000000000000, 2000000000000000000, 3000000000000000000,
    4000000000000000000, 5000000000000000000, 6000000000000000000, 7000000000000000000, 8000000000000000000,
    9000000000000000000, 10000000000000000000ull};

const std::vector<Amount> Currency::DECIMAL_PLACES = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000,
    1000000000, 10000000000, 100000000000, 1000000000000, 10000000000000, 100000000000000, 1000000000000000,
    10000000000000000, 100000000000000000, 1000000000000000000, 10000000000000000000ull};

Currency::Currency(bool is_testnet)
    : is_testnet(is_testnet)
    , max_block_height(parameters::CRYPTONOTE_MAX_BLOCK_NUMBER)
    , max_block_blob_size(parameters::CRYPTONOTE_MAX_BLOCK_BLOB_SIZE)
    , max_tx_size(parameters::CRYPTONOTE_MAX_TX_SIZE)
    , public_address_base58_prefix(parameters::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX)
    , mined_money_unlock_window(parameters::CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW)
    , timestamp_check_window(parameters::BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW)
    , block_future_time_limit(parameters::CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT)
    , money_supply(parameters::MONEY_SUPPLY)
    , emission_speed_factor(parameters::EMISSION_SPEED_FACTOR)
    , reward_blocks_window(parameters::CRYPTONOTE_REWARD_BLOCKS_WINDOW)
    , block_granted_full_reward_zone(parameters::CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE)
    , miner_tx_blob_reserved_size(parameters::CRYPTONOTE_COINBASE_BLOB_RESERVED_SIZE)
    , number_of_decimal_places(parameters::CRYPTONOTE_DISPLAY_DECIMAL_POINT)
    , minimum_fee(parameters::MINIMUM_FEE)
    , default_dust_threshold(parameters::DEFAULT_DUST_THRESHOLD)
    , difficulty_target(is_testnet ? 1 : parameters::DIFFICULTY_TARGET)
    , difficulty_window(parameters::DIFFICULTY_WINDOW(difficulty_target))
    , difficulty_lag(parameters::DIFFICULTY_LAG)
    , difficulty_cut(parameters::DIFFICULTY_CUT)
    , max_block_size_initial(parameters::MAX_BLOCK_SIZE_INITIAL)
    , max_block_size_growth_speed_numerator(parameters::MAX_BLOCK_SIZE_GROWTH_SPEED_NUMERATOR)
    , max_block_size_growth_speed_denominator(parameters::MAX_BLOCK_SIZE_GROWTH_SPEED_DENOMINATOR(difficulty_target))
    , locked_tx_allowed_delta_seconds(parameters::CRYPTONOTE_LOCKED_TX_ALLOWED_DELTA_SECONDS(difficulty_target))
    , locked_tx_allowed_delta_blocks(parameters::CRYPTONOTE_LOCKED_TX_ALLOWED_DELTA_BLOCKS)
    , upgrade_height_v2(parameters::UPGRADE_HEIGHT_V2)
    , upgrade_height_v3(parameters::UPGRADE_HEIGHT_V3)
	, upgrade_height_v4(parameters::UPGRADE_HEIGHT_V4)
    , current_transaction_version(CURRENT_TRANSACTION_VERSION)
	, hardfork_v1_height(parameters::HARDFORK_V1_HEIGHT)
	, hardfork_v2_height(parameters::HARDFORK_V2_HEIGHT)
	, hardfork_v3_height(parameters::HARDFORK_V3_HEIGHT)
	, timestamp_check_window_v2(parameters::BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW_V2)
	, block_future_time_limit_v2(parameters::CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT_V2)
	, difficulty_window_v2(parameters::DIFFICULTY_WINDOW_V2)
	, difficulty_blocks_count_v2(parameters::DIFFICULTY_BLOCKS_COUNT_V2) {
	if (is_testnet) {
		upgrade_height_v2 = 0;
		upgrade_height_v3 = static_cast<Height>(-1);
	}
	std::string genesis_coinbase_tx_hex =   "010a01ff0001ffffffffffff3f029b2e4c0281c0b02e7c53291a94d1d0cbff8883f8024f5142ee494ffbbd088071210152bfaef5e5681a5d7eaeaca878d53ed2a80445e4f6adc5550fc4b4b0e69b765a";

	BinaryArray miner_tx_blob;

	bool r = from_hex(genesis_coinbase_tx_hex, miner_tx_blob);
	seria::from_binary(genesis_block_template.base_transaction, miner_tx_blob);

	if (!r)
		throw std::runtime_error("Currency failed to parse coinbase tx from hard coded blob");

	genesis_block_template.major_version = 1;
	genesis_block_template.minor_version = 0;
	genesis_block_template.timestamp     = 0;
	genesis_block_template.nonce         = 70;
	if (is_testnet) 
	{
		++genesis_block_template.nonce;
	}
	genesis_block_hash = get_block_hash(genesis_block_template);
}

size_t Currency::sw_checkpoint_count() const { return is_testnet ? 1 : sizeof(CHECKPOINTS) / sizeof(*CHECKPOINTS); }

bool Currency::is_in_sw_checkpoint_zone(Height index) const {
	if (is_testnet)
		return index == 0;
	return index <= CHECKPOINTS[sw_checkpoint_count() - 1].height;
}

bool Currency::check_sw_checkpoint(Height index, const crypto::Hash &h, bool &is_sw_checkpoint) const {
	if (is_testnet || index == 0) {
		is_sw_checkpoint = (index == 0);
		return index == 0 ? h == genesis_block_hash : true;
	}
	auto it       = std::lower_bound(CHECKPOINTS, CHECKPOINTS + sw_checkpoint_count(), index,
	    [](const CheckpointData &da, uint32_t ma) { return da.height < ma; });
	is_sw_checkpoint = false;
	if (it == CHECKPOINTS + sw_checkpoint_count())
		return true;
	if (it->height != index)
		return true;
	is_sw_checkpoint = true;
	return common::pod_to_hex(h) == it->hash;
}

std::pair<Height, Hash> Currency::last_sw_checkpoint() const {
	if (is_testnet || sw_checkpoint_count() == 0)
		return std::make_pair(0, genesis_block_hash);
	auto cp = CHECKPOINTS[sw_checkpoint_count() - 1];
	Hash ha{};
	common::pod_from_hex(cp.hash, ha);
	return std::make_pair(cp.height, ha);
}

uint8_t Currency::get_block_major_version_for_height(Height height) const {
	if (height <= upgrade_height_v2)
		return 1;
	if (height > upgrade_height_v2 && height <= upgrade_height_v3)
		return 2;
	if (height > upgrade_height_v3 && height <= upgrade_height_v4)
		return 3;
	return 4;  
}

uint32_t Currency::block_granted_full_reward_zone_by_block_version(uint8_t block_major_version) const {
	if (block_major_version >= 3)
		return block_granted_full_reward_zone;
	if (block_major_version == 2)
		return cryonerocoin::parameters::CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE_V2;
	return cryonerocoin::parameters::CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE_V1;
}

Amount Currency::calc_base_reward(uint8_t block_major_version, Amount already_generated_coins) const
{
	Amount base_reward = (money_supply - already_generated_coins) >> emission_speed_factor;
	if (block_major_version >= 4)
	{
		if (base_reward >= cryonerocoin::parameters::DIFFICULTY_SCATTER_V2)
			base_reward = base_reward / cryonerocoin::parameters::DIFFICULTY_SCATTER_V2;
	}
	return base_reward;
}

bool Currency::get_block_reward(uint8_t block_major_version, size_t effective_median_size, size_t current_block_size,
    Amount already_generated_coins, Amount fee, Amount *reward, SignedAmount *emission_change) const 
{
	assert(already_generated_coins <= money_supply);
	assert(emission_speed_factor > 0 && emission_speed_factor <= 8 * sizeof(Amount));
	Amount base_reward = calc_base_reward(block_major_version, already_generated_coins);
	Amount penalized_base_reward = get_penalized_amount(base_reward, effective_median_size, current_block_size);
	Amount penalized_fee = block_major_version >= 2 ? get_penalized_amount(fee, effective_median_size, current_block_size) : fee;
	*emission_change = penalized_base_reward - (fee - penalized_fee);
	*reward          = penalized_base_reward + penalized_fee;
	return true;
}



uint32_t Currency::max_block_cumulative_size(Height height) const {
	assert(height <= std::numeric_limits<uint64_t>::max() / max_block_size_growth_speed_numerator);
	uint64_t max_size = static_cast<uint64_t>(
	    max_block_size_initial +
	    (height * max_block_size_growth_speed_numerator) / max_block_size_growth_speed_denominator);
	assert(max_size >= max_block_size_initial);
	return static_cast<uint32_t>(max_size);
}

uint32_t Currency::max_transaction_allowed_size(uint32_t effective_block_size_median) const {
	assert(effective_block_size_median * 2 > miner_tx_blob_reserved_size);

	return std::min(max_tx_size, effective_block_size_median * 2 - miner_tx_blob_reserved_size);
}

bool Currency::construct_miner_tx(uint8_t block_major_version, Height height, size_t effective_median_size,
    Amount already_generated_coins, size_t current_block_size, Amount fee, const AccountPublicAddress &miner_address,
    Transaction *tx, const BinaryArray &extra_nonce, size_t max_outs) const {
	tx->inputs.clear();
	tx->outputs.clear();
	tx->extra.clear();

	KeyPair txkey = crypto::random_keypair();
	add_transaction_public_key_to_extra(tx->extra, txkey.public_key);
	if (!extra_nonce.empty()) {
		if (!add_extra_nonce_to_transaction_extra(tx->extra, extra_nonce)) {
			return false;
		}
	}

	CoinbaseInput in;
	in.block_index = height;

	Amount block_reward;
	SignedAmount emission_change;
	if (!get_block_reward(block_major_version, effective_median_size, current_block_size, already_generated_coins, fee,
	        &block_reward, &emission_change)) {

		return false;
	}

	std::vector<Amount> out_amounts;
	decompose_amount(block_reward, default_dust_threshold, &out_amounts);

	if (max_outs == 0)
		max_outs = 1;  // :)
	while (out_amounts.size() > max_outs) {
		out_amounts[out_amounts.size() - 2] += out_amounts.back();
		out_amounts.pop_back();
	}

	Amount summary_amounts = 0;
	for (size_t no = 0; no < out_amounts.size(); no++) {
		crypto::KeyDerivation derivation{};
		crypto::PublicKey out_ephemeral_pub_key{};

		bool r = crypto::generate_key_derivation(miner_address.view_public_key, txkey.secret_key, derivation);

		if (!r) {

			return false;
		}

		r = crypto::derive_public_key(derivation, no, miner_address.spend_public_key, out_ephemeral_pub_key);

		if (!r) {

			return false;
		}

		KeyOutput tk;
		tk.key = out_ephemeral_pub_key;

		TransactionOutput out;
		summary_amounts += out.amount = out_amounts[no];
		out.target                    = tk;
		tx->outputs.push_back(out);
	}

	if (summary_amounts != block_reward) {

		return false;
	}

	tx->version = current_transaction_version;
	// lock
	tx->unlock_time = height + mined_money_unlock_window;
	tx->inputs.push_back(in);
	return true;
}

uint64_t Currency::get_penalized_amount(uint64_t amount, size_t median_size, size_t current_block_size) {
	static_assert(sizeof(size_t) >= sizeof(uint32_t), "size_t is too small");
	assert(current_block_size <= 2 * median_size);
	assert(median_size <= std::numeric_limits<uint32_t>::max());
	assert(current_block_size <= std::numeric_limits<uint32_t>::max());

	if (amount == 0) {
		return 0;
	}
	if (current_block_size <= median_size) {
		return amount;
	}

	uint64_t product_hi;
	uint64_t product_lo =
	    mul128(amount, current_block_size * (UINT64_C(2) * median_size - current_block_size), &product_hi);

	uint64_t penalized_amount_hi;
	uint64_t penalized_amount_lo;
	div128_32(product_hi, product_lo, static_cast<uint32_t>(median_size), &penalized_amount_hi, &penalized_amount_lo);
	div128_32(penalized_amount_hi, penalized_amount_lo, static_cast<uint32_t>(median_size), &penalized_amount_hi,
	    &penalized_amount_lo);

	assert(0 == penalized_amount_hi);
	assert(penalized_amount_lo < amount);

	return penalized_amount_lo;
}

std::string Currency::get_account_address_as_str(uint64_t prefix, const AccountPublicAddress &adr) {
	BinaryArray ba = seria::to_binary(adr);
	return common::base58::encode_addr(prefix, ba);
}

bool Currency::parse_account_address_string(uint64_t *prefix, AccountPublicAddress *adr, const std::string &str) {
	BinaryArray data;

	if (!common::base58::decode_addr(str, prefix, &data))
		return false;
	try {
		seria::from_binary(*adr, data);
	} catch (const std::exception &) {
		return false;
	}
	return key_isvalid(adr->spend_public_key) && key_isvalid(adr->view_public_key);
}

std::string Currency::account_address_as_string(const AccountPublicAddress &account_public_address) const {
	return get_account_address_as_str(public_address_base58_prefix, account_public_address);
}

bool Currency::parse_account_address_string(const std::string &str, AccountPublicAddress *addr) const {
	uint64_t prefix;
	if (!parse_account_address_string(&prefix, addr, str)) {
		return false;
	}
	if (prefix != public_address_base58_prefix) {

		return false;
	}
	return true;
}

static std::string ffw(cryonerocoin::Amount am, size_t digs) {
	std::string result = common::to_string(am);
	if (result.size() < digs)
		result = std::string(digs - result.size(), '0') + result;
	return result;
}

std::string Currency::format_amount(size_t number_of_decimal_places, Amount amount) {
	cryonerocoin::Amount ia = amount / DECIMAL_PLACES.at(number_of_decimal_places);
	cryonerocoin::Amount fa = amount - ia * DECIMAL_PLACES.at(number_of_decimal_places);
	std::string result;
	while (ia >= 1000) {
		result = "'" + ffw(ia % 1000, 3) + result;
		ia /= 1000;
	}
	result = std::to_string(ia) + result;
	if (fa != 0) {  // cents
		result += "." + ffw(fa / DECIMAL_PLACES.at(number_of_decimal_places - 2), 2);
		fa %= DECIMAL_PLACES.at(number_of_decimal_places - 2);
	}
	if (fa != 0) {
		result += "'" + ffw(fa / 1000, 3);
		fa %= 1000;
	}
	if (fa != 0)
		result += "'" + ffw(fa, 3);
	return result;
}

std::string Currency::format_amount(size_t number_of_decimal_places, SignedAmount amount) {
	std::string s = Currency::format_amount(number_of_decimal_places, static_cast<Amount>(std::abs(amount)));
	return amount < 0 ? "-" + s : s;
}

bool Currency::parse_amount(size_t number_of_decimal_places, const std::string &str, Amount *amount) {
	std::string str_amount = str;
	boost::algorithm::trim(str_amount);
	boost::algorithm::erase_all(str_amount, "'");

	size_t point_index = str_amount.find_first_of('.');
	size_t fraction_size;
	if (std::string::npos != point_index) {
		fraction_size = str_amount.size() - point_index - 1;
		while (number_of_decimal_places < fraction_size && '0' == str_amount.back()) {
			str_amount.erase(str_amount.size() - 1, 1);
			--fraction_size;
		}
		if (number_of_decimal_places < fraction_size) {
			return false;
		}
		str_amount.erase(point_index, 1);
	} else {
		fraction_size = 0;
	}

	if (str_amount.empty()) {
		return false;
	}

	if (!std::all_of(str_amount.begin(), str_amount.end(), ::isdigit)) {
		return false;
	}

	if (fraction_size < number_of_decimal_places) {
		str_amount.append(number_of_decimal_places - fraction_size, '0');
	}
	std::istringstream stream(str_amount);
	stream >> *amount;
	return !stream.fail();
}

Difficulty Currency::next_difficulty(Height block_index,
	std::vector<Timestamp> timestamps, std::vector<Difficulty> cumulative_difficulties) const {
	if (block_index <= hardfork_v2_height)
	{
		return next_difficulty_v1(block_index, timestamps, cumulative_difficulties);
	}

	return next_difficulty_v2(timestamps, cumulative_difficulties);
}

Difficulty Currency::next_difficulty_v1(Height block_index, std::vector<Timestamp> timestamps, std::vector<Difficulty> cumulative_difficulties) const
{
	std::vector<Timestamp> timestamps_o(timestamps);
	std::vector<Difficulty> cumulativeDifficulties_o(cumulative_difficulties);
	size_t c_difficultyWindow = difficulty_window;
	size_t c_difficultyCut = difficulty_cut;
	assert(difficulty_window >= 2);

	if (timestamps.size() > c_difficultyWindow) {
		timestamps.resize(c_difficultyWindow);
		cumulative_difficulties.resize(c_difficultyWindow);
	}

	size_t length = timestamps.size();
	assert(length == cumulative_difficulties.size());
	assert(length <= c_difficultyWindow);
	if (length <= 1) {
		return 1;
	}

	sort(timestamps.begin(), timestamps.end());

	size_t cutBegin, cutEnd;
	assert(2 * c_difficultyCut <= c_difficultyWindow - 2);
	if (length <= c_difficultyWindow - 2 * c_difficultyCut) {
		cutBegin = 0;
		cutEnd = length;
	}
	else {
		cutBegin = (length - (c_difficultyWindow - 2 * c_difficultyCut) + 1) / 2;
		cutEnd = cutBegin + (c_difficultyWindow - 2 * c_difficultyCut);
	}
	assert(cutBegin + 2 <= cutEnd && cutEnd <= length);
	Timestamp timeSpan = timestamps[cutEnd - 1] - timestamps[cutBegin];
	if (timeSpan == 0) {
		timeSpan = 1;
	}

	Difficulty totalWork = cumulative_difficulties[cutEnd - 1] - cumulative_difficulties[cutBegin];
	assert(totalWork > 0);

	uint64_t low, high;
	low = mul128(totalWork, difficulty_target, &high);
	if (high != 0 || std::numeric_limits<uint64_t>::max() - low < (timeSpan - 1)) {
		return 0;
	}

	if (block_index >= hardfork_v1_height) {
		if (high != 0) {
			return 0;
		}

		c_difficultyWindow = 17;
		c_difficultyCut = 0;

		assert(c_difficultyWindow >= 2);

		size_t t_difficultyWindow = c_difficultyWindow;
		if (c_difficultyWindow > timestamps.size()) {
			t_difficultyWindow = timestamps.size();
		}
		std::vector<Timestamp> timestamps_tmp(timestamps_o.end() - t_difficultyWindow, timestamps_o.end());
		std::vector<Difficulty> cumulativeDifficulties_tmp(cumulativeDifficulties_o.end() - t_difficultyWindow, cumulativeDifficulties_o.end());

		length = timestamps_tmp.size();
		assert(length == cumulativeDifficulties_tmp.size());
		assert(length <= c_difficultyWindow);
		if (length <= 1) {
			return 1;
		}

		sort(timestamps_tmp.begin(), timestamps_tmp.end());

		assert(2 * c_difficultyCut <= c_difficultyWindow - 2);
		if (length <= c_difficultyWindow - 2 * c_difficultyCut) {
			cutBegin = 0;
			cutEnd = length;
		}
		else {
			cutBegin = (length - (c_difficultyWindow - 2 * c_difficultyCut) + 1) / 2;
			cutEnd = cutBegin + (c_difficultyWindow - 2 * c_difficultyCut);
		}
		assert(/*cut_begin >= 0 &&*/ cutBegin + 2 <= cutEnd && cutEnd <= length);
		timeSpan = timestamps_tmp[cutEnd - 1] - timestamps_tmp[cutBegin];
		if (timeSpan == 0) {
			timeSpan = 1;
		}

		totalWork = cumulativeDifficulties_tmp[cutEnd - 1] - cumulativeDifficulties_tmp[cutBegin];
		assert(totalWork > 0);

		low = mul128(totalWork, difficulty_target, &high);
		if (high != 0 || std::numeric_limits<uint64_t>::max() - low < (timeSpan - 1)) {
			return 0;
		}
		uint64_t nextDiffZ = low / timeSpan;
		if (nextDiffZ <= 100) {
			nextDiffZ = 100;
		}
		return nextDiffZ;
	}

	return (low + timeSpan - 1) / timeSpan;
}


Difficulty Currency::next_difficulty_v2(std::vector<Timestamp> timestamps, std::vector<Difficulty> cumulative_difficulties) const
{
	int64_t T = difficulty_target;
	int64_t N = difficulty_window_v2;
	int64_t FTL = block_future_time_limit_v2;
	int64_t L(0), ST, sum_3_ST(0), next_D, prev_D;

	if (timestamps.size() <= static_cast<uint64_t>(N))
	{
		return 1000;
	}

	for (int64_t i = 1; i <= N; i++)
	{
		ST = std::max(-FTL, std::min(static_cast<int64_t>(timestamps[i]) - static_cast<int64_t>(timestamps[i - 1]), 6 * T));

		L += ST * i;

		if (i > N - 3)
		{
			sum_3_ST += ST;
		}
	}

	next_D = (cumulative_difficulties[N] - cumulative_difficulties[0]) * T * (N + 1) * 99 / (100 * 2 * L);
	prev_D = cumulative_difficulties[N] - cumulative_difficulties[N - 1];

	if (sum_3_ST < (8 * T) / 10)
	{
		next_D = (prev_D * 110) / 100;
	}

	return static_cast<Difficulty>(next_D);
}

bool Currency::check_proof_of_work_v1(const Hash &long_block_hash,
    const BlockTemplate &block,
    Difficulty current_difficulty) const {
	if (block.major_version != 1) {
		return false;
	}
	return check_hash(long_block_hash, current_difficulty);
}

bool Currency::check_proof_of_work_v2(const Hash &long_block_hash,
    const BlockTemplate &block,
    Difficulty current_difficulty) const {
	if (block.major_version < 2) {
		return false;
	}
	TransactionExtraMergeMiningTag mm_tag;
	if (!get_merge_mining_tag_from_extra(block.parent_block.base_transaction.extra, mm_tag)) {

		return false;
	}
	if (8 * sizeof(genesis_block_hash) < block.parent_block.blockchain_branch.size()) {
		return false;
	}
	crypto::Hash aux_blocks_merkle_root = crypto::tree_hash_from_branch(block.parent_block.blockchain_branch.data(),
	    block.parent_block.blockchain_branch.size(), get_auxiliary_block_header_hash(block), &genesis_block_hash);

	if (aux_blocks_merkle_root != mm_tag.merkle_root) {

		return false;
	}
	if (!check_hash(long_block_hash, current_difficulty)) {
		return false;
	}
	return true;
}

bool Currency::check_proof_of_work(const Hash &long_block_hash,
    const BlockTemplate &block,
    Difficulty current_difficulty) const {
	switch (block.major_version) {
	case 1:
		return check_proof_of_work_v1(long_block_hash, block, current_difficulty);
	case 2:
	case 3:
	case 4:
		return check_proof_of_work_v2(long_block_hash, block, current_difficulty);
	}

	return false;
}

bool Currency::is_dust(Amount amount) {
	auto pretty_it = std::lower_bound(std::begin(PRETTY_AMOUNTS), std::end(PRETTY_AMOUNTS), amount);
	return pretty_it == std::end(Currency::PRETTY_AMOUNTS) || *pretty_it != amount ||
	       amount < 1000000;  // After fork, dust definition will change
}

Hash cryonerocoin::get_transaction_inputs_hash(const TransactionPrefix &tx) {
	BinaryArray ba = seria::to_binary(tx.inputs);
	Hash new_hash  = crypto::cn_fast_hash(ba.data(), ba.size());
	return new_hash;
}

Hash cryonerocoin::get_transaction_prefix_hash(const TransactionPrefix &tx) {
	const TransactionPrefix &prefix = tx;
	BinaryArray ba                  = seria::to_binary(prefix);
	Hash new_hash                   = crypto::cn_fast_hash(ba.data(), ba.size());
	return new_hash;
}

Hash cryonerocoin::get_transaction_hash(const Transaction &tx) {
	BinaryArray ba = seria::to_binary(tx);
	Hash new_hash  = crypto::cn_fast_hash(ba.data(), ba.size());
	return new_hash;
}

static Hash get_transaction_tree_hash(const BlockTemplate &bh) {
	std::vector<Hash> transaction_hashes;
	transaction_hashes.reserve(bh.transaction_hashes.size() + 1);
	transaction_hashes.push_back(get_object_hash(bh.base_transaction));
	transaction_hashes.insert(transaction_hashes.end(), bh.transaction_hashes.begin(), bh.transaction_hashes.end());
	Hash tree_hash = crypto::tree_hash(transaction_hashes.data(), transaction_hashes.size());
	return tree_hash;
}

static BinaryArray get_block_hashing_binary_array(const BlockTemplate &bh) {
	BinaryArray ba = seria::to_binary(static_cast<const BlockHeader &>(bh));

	Hash tree_hash = get_transaction_tree_hash(bh);
	append(ba, std::begin(tree_hash.data), std::end(tree_hash.data));
	auto tx_count = common::get_varint_data(bh.transaction_hashes.size() + 1);
	append(ba, tx_count.begin(), tx_count.end());

	return ba;
}

Hash cryonerocoin::get_block_hash(const BlockTemplate &bh) {
	BinaryArray ba2 = get_block_hashing_binary_array(bh);

	if (bh.major_version >= 2) {
		auto serializer        = make_parent_block_serializer(bh, true, false);
		BinaryArray parent_ba2 = seria::to_binary(serializer);
		append(ba2, parent_ba2.begin(), parent_ba2.end());
	}
	Hash new_hash2 = get_object_hash(ba2);
	return new_hash2;
}

Hash cryonerocoin::get_auxiliary_block_header_hash(const BlockTemplate &bh) {
	return get_object_hash(get_block_hashing_binary_array(bh));
}

Hash cryonerocoin::get_block_long_hash(const BlockTemplate &bh, crypto::CryptoNightContext &crypto_ctx) {
	if (bh.major_version == 1) {
		auto raw_hashing_block = get_block_hashing_binary_array(bh);
		return crypto_ctx.cn_slow_hash(raw_hashing_block.data(), raw_hashing_block.size());
	}
	if (bh.major_version >= 2 && bh.major_version < 4) {
		auto serializer               = make_parent_block_serializer(bh, true, true);
		BinaryArray raw_hashing_block = seria::to_binary(serializer);
		return crypto_ctx.cn_slow_hash(raw_hashing_block.data(), raw_hashing_block.size());
	}
	if (bh.major_version >= 4) {
		auto serializer = make_parent_block_serializer(bh, true, true);
		BinaryArray raw_hashing_block = seria::to_binary(serializer);
		return crypto_ctx.cn_lite_slow_hash_v1(raw_hashing_block.data(), raw_hashing_block.size());
	}

	throw std::runtime_error("Unknown block major version.");
}

Height Currency::get_timestamp_check_window(Height height) const
{
	return height >= hardfork_v2_height ? timestamp_check_window_v2 : timestamp_check_window;
}

Timestamp Currency::get_block_future_time_limit(Height height) const
{
	return height >= hardfork_v2_height ? block_future_time_limit_v2 : block_future_time_limit;
}

Height cryonerocoin::Currency::get_difficulty_blocks_count(Height height) const
{
	if (height <= hardfork_v2_height)
	{
		return difficulty_blocks_count();
	}

	return difficulty_blocks_count_v2;
}