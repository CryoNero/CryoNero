// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2018-2019, The Naza developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include "TransactionBuilder.hpp"
#include <iostream>
#include "BlockChain.hpp"
#include "CryptoNoteTools.hpp"
#include "Currency.hpp"
#include "Wallet.hpp"
#include "common/string.hpp"
#include "crypto/crypto.hpp"
#include "crypto/random.h"
#include "http/JsonRpc.hpp"
#include "seria/BinaryOutputStream.hpp"

using namespace nazacoin;

bool TransactionBuilder::derive_public_key(const AccountPublicAddress &to,
	const SecretKey &tx_key,
	size_t output_index,
	PublicKey &ephemeral_key) {
	KeyDerivation derivation;
	if (!generate_key_derivation(to.view_public_key, tx_key, derivation))
		return false;
	return crypto::derive_public_key(derivation, output_index, to.spend_public_key, ephemeral_key);
}

TransactionBuilder::TransactionBuilder(const Currency &currency, UnlockMoment unlock_time) {
	m_transaction.version = currency.current_transaction_version;
	m_transaction.unlock_time = unlock_time;
}

void TransactionBuilder::set_payment_id(const Hash &hash) {
	BinaryArray payment_id_blob;
	set_payment_id_to_transaction_extra_nonce(payment_id_blob, reinterpret_cast<const Hash &>(hash));
	set_extra_nonce(payment_id_blob);
}

void TransactionBuilder::set_extra_nonce(const BinaryArray &nonce) {
	TransactionExtraNonce extra_nonce = { nonce };
	m_extra.set(extra_nonce);
	m_transaction.extra = m_extra.serialize();
}

size_t TransactionBuilder::add_output(uint64_t amount, const AccountPublicAddress &to) {
	m_outputs_amount += amount;

	OutputDesc desc;
	desc.amount = amount;
	desc.addr = to;
	m_output_descs.push_back(std::move(desc));
	return m_output_descs.size() - 1;
}

static bool APIOutputLessGlobalIndex(const api::Output &a, const api::Output &b) {
	return a.global_index < b.global_index;
}
static bool APIOutputEqualGlobalIndex(const api::Output &a, const api::Output &b) {
	return a.global_index == b.global_index;
}
bool TransactionBuilder::generate_key_image_helper(const AccountKeys &ack, const PublicKey &tx_public_key,
	size_t real_output_index, KeyPair &in_ephemeral, KeyImage &ki) {
	KeyDerivation recv_derivation;
	bool r = generate_key_derivation(tx_public_key, ack.view_secret_key, recv_derivation);
	if (!r)
		return false;
	r = crypto::derive_public_key(
		recv_derivation, real_output_index, ack.address.spend_public_key, in_ephemeral.public_key);
	if (!r)
		return false;
	crypto::derive_secret_key(recv_derivation, real_output_index, ack.spend_secret_key, in_ephemeral.secret_key);
	crypto::generate_key_image(in_ephemeral.public_key, in_ephemeral.secret_key, ki);
	return true;
}

std::vector<uint32_t> TransactionBuilder::absolute_output_offsets_to_relative(const std::vector<uint32_t> &off) {
	auto copy = off;
	for (auto i = 1; i < copy.size(); ++i) {
		copy[i] = off[i] - off[i - 1];
	}
	return copy;
}

size_t TransactionBuilder::add_input(const AccountKeys &sender_keys,
	api::Output real_output,
	const std::vector<api::Output> &mix_outputs) {
	m_inputs_amount += real_output.amount;

	InputDesc desc;
	desc.input.amount = real_output.amount;
	desc.outputs = mix_outputs;
	std::sort(desc.outputs.begin(), desc.outputs.end(), APIOutputLessGlobalIndex);
	desc.real_output_index =
		std::lower_bound(desc.outputs.begin(), desc.outputs.end(), real_output, APIOutputLessGlobalIndex) -
		desc.outputs.begin();
	desc.outputs.insert(desc.outputs.begin() + desc.real_output_index, real_output);

	if (!generate_key_image_helper(sender_keys, real_output.transaction_public_key, real_output.index_in_transaction,
		desc.eph_keys, desc.input.key_image))
		throw std::runtime_error("generating key_image failed");
	if (desc.input.key_image != real_output.key_image)
		throw std::runtime_error("generated key_image does not match input");

	for (const auto &out : desc.outputs) {
		if (out.amount != real_output.amount)  
			throw std::runtime_error("Mixin outputs with different amounts is not allowed");
		desc.input.output_indexes.push_back(out.global_index);
	}

	desc.input.output_indexes = absolute_output_offsets_to_relative(desc.input.output_indexes);
	m_input_descs.push_back(std::move(desc));
	return m_input_descs.size() - 1;
}

KeyPair TransactionBuilder::deterministic_keys_from_seed(const Hash &tx_inputs_hash, const Hash &tx_derivation_seed) {
	BinaryArray ba;
	common::append(ba, std::begin(tx_inputs_hash.data), std::end(tx_inputs_hash.data));
	common::append(ba, std::begin(tx_derivation_seed.data), std::end(tx_derivation_seed.data));

	KeyPair tx_keys{};
	crypto::hash_to_scalar(ba.data(), ba.size(), tx_keys.secret_key);
	crypto::secret_key_to_public_key(tx_keys.secret_key, tx_keys.public_key);
	return tx_keys;
}

KeyPair TransactionBuilder::deterministic_keys_from_seed(const TransactionPrefix &tx, const Hash &tx_derivation_seed) {
	auto tx_inputs_hash = get_transaction_inputs_hash(tx);
	return deterministic_keys_from_seed(tx_inputs_hash, tx_derivation_seed);
}

Transaction TransactionBuilder::sign(const Hash &tx_derivation_seed) {
	std::shuffle(m_output_descs.begin(), m_output_descs.end(), crypto::random_engine<size_t>{});
	std::shuffle(m_input_descs.begin(), m_input_descs.end(), crypto::random_engine<size_t>{});
	std::stable_sort(m_output_descs.begin(), m_output_descs.end(), OutputDesc::less_amount);
	std::stable_sort(m_input_descs.begin(), m_input_descs.end(), InputDesc::less_amount);

	m_transaction.inputs.resize(m_input_descs.size());
	for (auto i = 0; i != m_input_descs.size(); ++i)
		m_transaction.inputs.at(i) = std::move(m_input_descs[i].input);
	auto tx_keys = deterministic_keys_from_seed(m_transaction, tx_derivation_seed);

	TransactionExtraPublicKey pk = { tx_keys.public_key };
	m_extra.set(pk);
	m_transaction.extra = m_extra.serialize();
	m_transaction.outputs.resize(m_output_descs.size());
	for (auto i = 0; i != m_output_descs.size(); ++i) {
		KeyOutput out_key;
		if (!derive_public_key(m_output_descs[i].addr, tx_keys.secret_key, i, out_key.key))
			throw std::runtime_error("output keys detected as corrupted during output key derivation");
		TransactionOutput out; 
		out.amount = m_output_descs[i].amount;
		out.target = out_key;
		m_transaction.outputs.at(i) = out;
	}

	auto hash = get_transaction_prefix_hash(m_transaction);
	m_transaction.signatures.resize(m_input_descs.size());
	for (auto i = 0; i != m_input_descs.size(); ++i) {
		const KeyInput &input = boost::get<KeyInput>(m_transaction.inputs.at(i));
		const InputDesc &desc = m_input_descs[i];
		std::vector<Signature> signatures;
		std::vector<const PublicKey *> keys_ptrs;
		for (const auto &o : desc.outputs) {
			keys_ptrs.push_back(&o.public_key);
		}
		signatures.resize(keys_ptrs.size(), Signature{});
		if (!generate_ring_signature(hash, input.key_image, keys_ptrs, desc.eph_keys.secret_key, desc.real_output_index,
			signatures.data())) {
			throw std::runtime_error("output keys detected as corrupted during ring signing");
		}
		m_transaction.signatures.at(i) = signatures;
	}
	return m_transaction;
}

UnspentSelector::UnspentSelector(logging::ILogger &logger, const Currency &currency, Unspents &&unspents)
	: m_log(logger, "UnspentSelector"), m_currency(currency), m_unspents(std::move(unspents)) {}

void UnspentSelector::reset(Unspents &&unspents) {
	m_unspents = std::move(unspents);
	m_used_unspents.clear();
	m_optimization_unspents.clear();
	m_used_total = 0;
	m_inputs_count = 0;
	m_ra_amounts.clear();
}

void UnspentSelector::add_mixed_inputs(const SecretKey &view_secret_key,
	const std::unordered_map<PublicKey, WalletRecord> &wallet_records, TransactionBuilder *builder, uint32_t anonymity,
	api::nazad::GetRandomOutputs::Response &&ra_response) {
	for (const auto & uu : m_used_unspents) {
		std::vector<api::Output> mix_outputs;
		auto &our_ra_outputs = ra_response.outputs[uu.amount];
		while (mix_outputs.size() < anonymity + 1) {
			if (our_ra_outputs.empty())
				throw api::walletd::CreateTransaction::Error(api::walletd::CreateTransaction::NOT_ENOUGH_ANONYMITY,
					"Not enough anonymity for amount " + common::to_string(uu.amount));
			mix_outputs.push_back(std::move(our_ra_outputs.back()));
			our_ra_outputs.pop_back();
		}
		std::sort(mix_outputs.begin(), mix_outputs.end(), APIOutputLessGlobalIndex);
		mix_outputs.erase(
			std::unique(mix_outputs.begin(), mix_outputs.end(), APIOutputEqualGlobalIndex), mix_outputs.end());
		int best_distance = 0;
		auto best_index = mix_outputs.size();
		for (auto i = 0; i != mix_outputs.size(); ++i) {
			int distance = abs(int(uu.global_index) - int(mix_outputs[i].global_index));
			if (best_index == mix_outputs.size() || distance < best_distance) {
				best_index = i;
				best_distance = distance;
			}
		}
		invariant(best_index != mix_outputs.size(), "");
		mix_outputs.erase(mix_outputs.begin() + best_index);
		AccountKeys sender_keys;
		sender_keys.view_secret_key = view_secret_key;
		if (!m_currency.parse_account_address_string(uu.address, &sender_keys.address))
			throw json_rpc::Error(json_rpc::INVALID_PARAMS, "Could not parse address " + uu.address);
		auto rit = wallet_records.find(sender_keys.address.spend_public_key);
		if (rit == wallet_records.end() || rit->second.spend_public_key != sender_keys.address.spend_public_key)
			throw json_rpc::Error(json_rpc::INVALID_PARAMS, "No keys in wallet for address " + uu.address);
		sender_keys.spend_secret_key = rit->second.spend_secret_key;
		builder->add_input(sender_keys, uu, mix_outputs);
	}
}

constexpr Amount fake_large = 1000000000000000000;  
constexpr size_t OPTIMIZATIONS_PER_TX = 50;
constexpr size_t OPTIMIZATIONS_PER_TX_AGGRESSIVE = 200;
constexpr size_t MEDIAN_PERCENT = 12;  
constexpr size_t MEDIAN_PERCENT_AGGRESSIVE = 30;  
constexpr size_t STACK_OPTIMIZATION_THRESHOLD = 20; 
constexpr size_t TWO_THRESHOLD = 10;  

std::string UnspentSelector::select_optimal_outputs(Height block_height, Timestamp block_time, Height confirmed_height,
	size_t effective_median_size, size_t anonymity, Amount total_amount, size_t total_outputs, Amount fee_per_byte,
	std::string optimization_level, Amount *change) {
	effective_median_size = (120 * effective_median_size) / 100;  

	HaveCoins have_coins;
	size_t max_digits;
	DustCoins dust_coins;
	create_have_coins(block_height, block_time, confirmed_height, &have_coins, &dust_coins, &max_digits);
	Amount fee = m_currency.minimum_fee;
	size_t optimizations = (optimization_level == "aggressive")
		? OPTIMIZATIONS_PER_TX_AGGRESSIVE
		: (optimization_level == "minimal") ? 9 : OPTIMIZATIONS_PER_TX;

	auto optimization_median_percent =
		(optimization_level == "aggressive") ? MEDIAN_PERCENT_AGGRESSIVE : MEDIAN_PERCENT;
	const size_t optimization_median = effective_median_size * optimization_median_percent / 100;
	while (true) {
		if (!select_optimal_outputs(&have_coins, &dust_coins, max_digits, total_amount + fee, anonymity, optimizations))
			return "NOT_ENOUGH_FUNDS";
		auto change_dust_fee = (m_used_total - total_amount - fee) % m_currency.default_dust_threshold;
		auto tx_size = get_maximum_tx_size(m_inputs_count, total_outputs + 8,
			anonymity);  // TODO - 8 is expected max change outputs
		if (tx_size > optimization_median && optimizations > 0) {
			unoptimize_amounts(&have_coins, &dust_coins);
			optimizations /= 2;
			if (optimizations < 10)
				optimizations = 0; 
			continue;
		}
		if (tx_size > effective_median_size)
			return "TRANSACTION_DOES_NOT_FIT_IN_BLOCK";
		auto size_fee = fee_per_byte * tx_size;
		if (fee + change_dust_fee >= size_fee) {
			*change = m_used_total - total_amount - fee - change_dust_fee;
			combine_optimized_unspents();
			std::string final_coins;
			for (const auto & uu : m_used_unspents)
				final_coins += " " + common::to_string(uu.amount);
			m_log(logging::INFO) << "Selected used_total=" << m_used_total << " for total_amount=" << total_amount
				<< ", final coins" << final_coins << std::endl;
			return std::string();
		}
		fee =
			((size_fee - change_dust_fee + m_currency.default_dust_threshold - 1) / m_currency.default_dust_threshold) *
			m_currency.default_dust_threshold;
		unoptimize_amounts(&have_coins, &dust_coins);
	}
}

void UnspentSelector::create_have_coins(Height block_height, Timestamp block_time, Height confirmed_height,
	HaveCoins *have_coins, DustCoins *dust_coins, size_t *max_digit) {
	*max_digit = 0;
	for (auto uit = m_unspents.rbegin(); uit != m_unspents.rend(); ++uit) {
		api::Output &un = *uit;
		if (un.height >= confirmed_height)  // unconfirmed
			continue;
		if (!m_currency.is_transaction_spend_time_unlocked(un.unlock_time, block_height, block_time))
			continue;
		if (!Currency::is_dust(un.amount)) {
			auto am = un.amount;
			size_t digit = 0;
			while (am > 9) {
				digit += 1;
				am /= 10;
			}
			*max_digit = std::max(*max_digit, digit);
			(*have_coins)[digit][static_cast<size_t>(am)].push_back(un);
		}
		else
			(*dust_coins)[un.amount].push_back(un);
	}
}

void UnspentSelector::combine_optimized_unspents() {
	for (auto &&un : m_optimization_unspents) {
		m_ra_amounts.push_back(un.amount);
	}
	m_used_unspents.insert(m_used_unspents.end(), m_optimization_unspents.begin(), m_optimization_unspents.end());
	m_optimization_unspents.clear();
}

void UnspentSelector::unoptimize_amounts(HaveCoins *have_coins, DustCoins *dust_coins) {
	for (auto &&un : m_optimization_unspents) {
		m_used_total -= un.amount;
		m_inputs_count -= 1;
		if (!un.dust) {
			auto am = un.amount;
			size_t digit = 0;
			while (am > 9) {
				digit += 1;
				am /= 10;
			}
			(*have_coins)[digit][static_cast<size_t>(am)].push_back(un);
		}
		else
			(*dust_coins)[un.amount].push_back(un);
	}
	m_optimization_unspents.clear();
}

void UnspentSelector::optimize_amounts(HaveCoins *have_coins, size_t max_digit, Amount total_amount) {
	m_log(logging::INFO) << "Sub optimizing amount=" << fake_large + total_amount - m_used_total
		<< " total_amount=" << total_amount << " used_total=" << m_used_total << std::endl;
	Amount digit_amount = 1;
	for (auto digit = 0; digit != max_digit + 1; ++digit, digit_amount *= 10) {
		if (m_used_total >= total_amount && digit_amount > m_used_total)  // No optimization far beyond requested sum
			break;
		Amount am = 10 - ((fake_large + total_amount + digit_amount - 1 - m_used_total) / digit_amount) % 10;
		auto dit = have_coins->find(digit);
		if (dit == have_coins->end())  // No coins for digit
			continue;
		size_t best_two_counts[2] = {};
		size_t best_weight = 0;
		for (const auto & ait : dit->second)
			for (const auto & bit : dit->second) {
				if ((ait.first + bit.first + am) % 10 == 0 &&
					(ait.second.size() >= TWO_THRESHOLD || bit.second.size() >= TWO_THRESHOLD) &&
					(ait.second.size() + bit.second.size()) > best_weight) {
					best_weight = ait.second.size() + bit.second.size();
					best_two_counts[0] = ait.first;
					best_two_counts[1] = bit.first;
				}
			}
		if (best_weight != 0) {
			m_log(logging::INFO) << "Found pair for digit=" << digit << " am=" << 10 - am << " coins=("
				<< best_two_counts[0] << ", " << best_two_counts[1] << ") sum weight=" << best_weight
				<< std::endl;
			for (size_t i = 0; i != 2; ++i) {
				auto &uns = dit->second[best_two_counts[i]];
				auto &un = uns.back();
				m_optimization_unspents.push_back(un);
				m_used_total += un.amount;
				m_inputs_count += 1;
				uns.pop_back();
				if (uns.empty())
					dit->second.erase(best_two_counts[i]);
				if (dit->second.empty())
					have_coins->erase(dit);
			}
			continue;
		}
		if (am == 10)
			continue;
		size_t best_single = 0;
		best_weight = 0;
		for (const auto & ait : dit->second)
			if ((ait.first + am) % 10 == 0) {
				best_single = ait.first;
				break;
			}
			else if (ait.first > 10 - am && ait.second.size() > best_weight) {
				best_weight = ait.second.size();
				best_single = ait.first;
			}
			if (best_single != 0) {
				m_log(logging::INFO) << "Found single for digit=" << digit << " am=" << 10 - am << " coin=" << best_single
					<< " weight=" << best_weight << std::endl;
				auto &uns = dit->second[best_single];
				auto &un = uns.back();
				m_optimization_unspents.push_back(un);
				m_used_total += un.amount;
				m_inputs_count += 1;
				uns.pop_back();
				if (uns.empty())
					dit->second.erase(best_single);
				if (dit->second.empty())
					have_coins->erase(dit);
				continue;
			}
			m_log(logging::INFO) << "Found nothing for digit=" << digit << std::endl;
	}
	m_log(logging::INFO) << "Sub optimized used_total=" << m_used_total << " for total=" << total_amount << std::endl;
}

bool UnspentSelector::select_optimal_outputs(HaveCoins *have_coins, DustCoins *dust_coins, size_t max_digit,
	Amount total_amount, size_t anonymity, size_t optimization_count) {

	m_log(logging::INFO) << "Optimizing amount=" << fake_large + total_amount - m_used_total
		<< " total_amount=" << total_amount << " used_total=" << m_used_total << std::endl;
	if (anonymity == 0) {
		if (m_used_total < total_amount) {

			auto duit = dust_coins->lower_bound(total_amount - m_used_total);
			if (duit != dust_coins->end()) {
				auto &un = duit->second.back();
				m_log(logging::INFO) << "Found single large dust coin=" << un.amount << std::endl;
				m_optimization_unspents.push_back(un);
				m_used_total += un.amount;
				m_inputs_count += 1;
				duit->second.pop_back();
				if (duit->second.empty())
					dust_coins->erase(duit);
			}
		}
		while (m_used_total < total_amount && !dust_coins->empty() && optimization_count >= 1) {
			auto duit = --dust_coins->end();
			auto &un = duit->second.back();
			m_log(logging::INFO) << "Found optimization dust coin=" << un.amount << std::endl;
			m_optimization_unspents.push_back(un);
			m_used_total += un.amount;
			m_inputs_count += 1;
			optimization_count -= 1;
			duit->second.pop_back();
			if (duit->second.empty())
				dust_coins->erase(duit);
		}
	}
	while (optimization_count >= 10) {
		size_t best_weight = STACK_OPTIMIZATION_THRESHOLD;
		std::vector<api::Output> *best_stack = nullptr;
		for (auto &hit : *have_coins)
			for (auto &ait : hit.second)
				if (ait.second.size() > best_weight) {
					best_weight = ait.second.size();
					best_stack = &ait.second;
				}
		if (!best_stack)
			break;
		for (int i = 0; i != 10; ++i) {
			auto &un = best_stack->back();
			m_log(logging::INFO) << "Found optimization stack for coin=" << un.amount << std::endl;
			m_optimization_unspents.push_back(un);
			m_used_total += un.amount;
			m_inputs_count += 1;
			optimization_count -= 1;
			best_stack->pop_back();  // Will never become empty because of threshold
		}
	}
	optimize_amounts(have_coins, max_digit, total_amount);
	if (m_used_total >= total_amount)
		return true;
	// Find smallest coin >= total_amount - used_total
	bool found = false;
	Amount digit_amount = 1;
	for (size_t digit = 0; !found && digit != max_digit + 1; ++digit, digit_amount *= 10) {
		auto dit = have_coins->find(digit);
		if (dit == have_coins->end())  // No coins for digit
			continue;
		for (auto ait = dit->second.begin(); ait != dit->second.end(); ++ait)
			if (!ait->second.empty() && ait->first * digit_amount >= total_amount - m_used_total) {
				m_log(logging::INFO) << "Found single large coin for digit=" << digit << " coin=" << ait->first
					<< std::endl;
				auto &uns = dit->second[ait->first];
				auto &un = uns.back();
				m_optimization_unspents.push_back(un);
				m_used_total += un.amount;
				m_inputs_count += 1;
				uns.pop_back();
				if (uns.empty())
					dit->second.erase(ait);
				if (dit->second.empty())
					have_coins->erase(dit);
				found = true;
				break;
			}
	}
	if (m_used_total >= total_amount)
		return true;
	unoptimize_amounts(have_coins, dust_coins);
	while (m_used_total < total_amount) {
		if (have_coins->empty() && (anonymity != 0 || dust_coins->empty()))
			return false;
		Amount ha_amount = 0;
		Amount du_amount = 0;
		if (!have_coins->empty()) {
			auto dit = --have_coins->end();
			auto ait = --dit->second.end();
			ha_amount = ait->second.back().amount;
		}
		if (anonymity == 0 && !dust_coins->empty()) {
			auto duit = --dust_coins->end();
			du_amount = duit->second.back().amount;
		}
		if (ha_amount > du_amount) {
			auto dit = --have_coins->end();
			auto ait = --dit->second.end();
			auto &uns = ait->second;
			auto &un = uns.back();
			m_log(logging::INFO) << "Found filler coin=" << un.amount << std::endl;
			m_optimization_unspents.push_back(un);
			m_used_total += un.amount;
			m_inputs_count += 1;
			uns.pop_back();
			if (uns.empty())
				dit->second.erase(ait);
			if (dit->second.empty())
				have_coins->erase(dit);
		}
		else {
			auto duit = --dust_coins->end();
			auto &un = duit->second.back();
			m_log(logging::INFO) << "Found filler dust coin=" << un.amount << std::endl;
			m_optimization_unspents.push_back(un);
			m_used_total += un.amount;
			m_inputs_count += 1;
			duit->second.pop_back();
			if (duit->second.empty())
				dust_coins->erase(duit);
		}
	}
	optimize_amounts(have_coins, max_digit, total_amount);
	return true;
}
