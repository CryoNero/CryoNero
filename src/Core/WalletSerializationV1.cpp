// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include "WalletSerializationV1.hpp"
#include "CryptoNoteTools.hpp"
#include "common/MemoryStreams.hpp"
#include "crypto/crypto.hpp"
#include "seria/BinaryInputStream.hpp"

using namespace common;
using namespace crypto;

namespace {


	struct WalletRecordDto {
		PublicKey spend_public_key{};
		SecretKey spend_secret_key{};
		uint64_t pending_balance = 0;
		uint64_t actual_balance = 0;
		uint64_t creation_timestamp = 0;
	};


	struct ObsoleteSpentOutputDto {
		uint64_t amount;
		Hash transaction_hash;
		uint32_t output_in_transaction;
		uint64_t wallet_index;
		crypto::Hash spending_transaction_hash;
	};


	struct ObsoleteChangeDto {
		Hash tx_hash;
		uint64_t amount;
	};


	struct UnlockTransactionJobDto {
		uint32_t block_height;
		Hash transaction_hash;
		uint64_t wallet_index;
	};


	struct KeysStorage {
		uint64_t creation_timestamp;

		crypto::PublicKey spend_public_key;
		crypto::SecretKey spend_secret_key;

		crypto::PublicKey view_public_key;
		crypto::SecretKey view_secret_key;
	};

	std::string read_cipher(common::IInputStream &source, const std::string &name) {
		std::string cipher;
		seria::BinaryInputStream s(source);
		s(cipher);  // , name

		return cipher;
	}

	std::string decrypt(const std::string &cipher, cryonerocoin::WalletSerializerV1::CryptoContext &crypto_ctx) {
		std::string plain;
		plain.resize(cipher.size());

		crypto::chacha8(cipher.data(), cipher.size(), crypto_ctx.key, crypto_ctx.iv, &plain[0]);
		return plain;
	}

	template<typename Object>
	void deserialize(Object &obj, const std::string &name, const std::string &plain) {
		MemoryInputStream stream(plain.data(), plain.size());
		seria::BinaryInputStream s(stream);
		s(obj);
	}

	template<typename Object>
	void deserialize_encrypted(Object &obj, const std::string &name,
		cryonerocoin::WalletSerializerV1::CryptoContext &crypto_ctx, common::IInputStream &source) {
		std::string cipher = read_cipher(source, name);
		std::string plain = decrypt(cipher, crypto_ctx);

		deserialize(obj, name, plain);
	}

}  // anonymous namespace

namespace seria {
	void ser(crypto::chacha8_iv &v, ISeria &s) { s.binary(v.data, sizeof(v.data)); }
	void ser_members(WalletRecordDto &v, ISeria &s) {
		seria_kv("spend_public_key", v.spend_public_key, s);
		seria_kv("spend_secret_key", v.spend_secret_key, s);
		seria_kv("pending_balance", v.pending_balance, s);
		seria_kv("actual_balance", v.actual_balance, s);
		seria_kv("creation_timestamp", v.creation_timestamp, s);
	}
	void ser_members(KeysStorage &v, ISeria &s) {
		seria_kv("creation_timestamp", v.creation_timestamp, s);
		seria_kv("spend_public_key", v.spend_public_key, s);
		seria_kv("spend_secret_key", v.spend_secret_key, s);
		seria_kv("view_public_key", v.view_public_key, s);
		seria_kv("view_secret_key", v.view_secret_key, s);
	}
}

namespace cryonerocoin {

	const uint32_t WalletSerializerV1::SERIALIZATION_VERSION = 5;

	void WalletSerializerV1::CryptoContext::inc_iv() {
		uint64_t *i = reinterpret_cast<uint64_t *>(&iv.data[0]);
		*i = (*i == std::numeric_limits<uint64_t>::max()) ? 0 : (*i + 1);
	}

	WalletSerializerV1::WalletSerializerV1(crypto::PublicKey &view_public_key, crypto::SecretKey &view_secret_key,
		std::vector<WalletRecord> &wallets_container)
		: m_view_public_key(view_public_key), m_view_secret_key(view_secret_key), m_wallets_container(wallets_container) {}

	void WalletSerializerV1::load(const crypto::chacha8_key &key, common::IInputStream &source) {
		seria::BinaryInputStream s(source);
		s.begin_object();

		uint32_t version = load_version(source);

		if (version > SERIALIZATION_VERSION) {
			throw std::runtime_error("WRONG_VERSION");
		}
		else if (version != 1) {
			load_wallet(source, key, version);
		}
		else {
			load_wallet_v1(source, key);
		}

		s.end_object();
	}

	void WalletSerializerV1::load_wallet(common::IInputStream &source, const crypto::chacha8_key &key, uint32_t version) {
		CryptoContext crypto_ctx;

		load_iv(source, crypto_ctx.iv);
		crypto_ctx.key = key;

		load_keys(source, crypto_ctx);
		check_keys();

		load_wallets(source, crypto_ctx);
	}

	void WalletSerializerV1::load_wallet_v1(common::IInputStream &source, const crypto::chacha8_key &key) {
		CryptoContext crypto_ctx;

		seria::BinaryInputStream encrypted(source);

		encrypted(crypto_ctx.iv);
		crypto_ctx.key = key;

		std::string cipher;
		encrypted(cipher);

		std::string plain = decrypt(cipher, crypto_ctx);

		MemoryInputStream decrypted_stream(plain.data(), plain.size());
		seria::BinaryInputStream serializer(decrypted_stream);

		load_wallet_v1_keys(serializer);
		check_keys();

		bool details_saved;
		serializer(details_saved);  // , "has_details"
	}

	void WalletSerializerV1::load_wallet_v1_keys(seria::ISeria &s) {
		KeysStorage keys;

		try {
			s(keys);
		}
		catch (const std::runtime_error &) {
			throw std::runtime_error("WRONG_PASSWORD");
		}

		m_view_public_key = keys.view_public_key;
		m_view_secret_key = keys.view_secret_key;

		WalletRecord wallet;
		wallet.spend_public_key = keys.spend_public_key;
		wallet.spend_secret_key = keys.spend_secret_key;
		wallet.creation_timestamp = static_cast<Timestamp>(keys.creation_timestamp);

		m_wallets_container.push_back(wallet);
	}

	uint32_t WalletSerializerV1::load_version(common::IInputStream &source) {
		seria::BinaryInputStream s(source);

		uint32_t version = std::numeric_limits<uint32_t>::max();
		s(version);

		return version;
	}

	void WalletSerializerV1::load_iv(common::IInputStream &source, crypto::chacha8_iv &iv) {
		seria::BinaryInputStream s(source);

		s.binary(static_cast<void *>(&iv.data), sizeof(iv.data));  // , "chacha_iv"
	}

	void WalletSerializerV1::load_keys(common::IInputStream &source, CryptoContext &crypto_ctx) {
		try {
			load_public_key(source, crypto_ctx);
			load_secret_key(source, crypto_ctx);
		}
		catch (const std::runtime_error &) {
			throw std::runtime_error("WRONG_PASSWORD");
		}
	}

	void WalletSerializerV1::load_public_key(common::IInputStream &source, CryptoContext &crypto_ctx) {
		deserialize_encrypted(m_view_public_key, "public_key", crypto_ctx, source);
		crypto_ctx.inc_iv();
	}

	void WalletSerializerV1::load_secret_key(common::IInputStream &source, CryptoContext &crypto_ctx) {
		deserialize_encrypted(m_view_secret_key, "secret_key", crypto_ctx, source);
		crypto_ctx.inc_iv();
	}

	void WalletSerializerV1::check_keys() {
		if (!keys_match(m_view_secret_key, m_view_public_key))
			throw std::runtime_error("Keys do not match");
	}



	void WalletSerializerV1::load_wallets(common::IInputStream &source, CryptoContext &crypto_ctx) {
		auto &index = m_wallets_container;

		uint64_t count = 0;
		deserialize_encrypted(count, "wallets_count", crypto_ctx, source);
		crypto_ctx.inc_iv();

		bool is_tracking_mode = false;  // init not required, but prevents warning

		for (auto i = 0; i < count; ++i) {
			WalletRecordDto dto;
			deserialize_encrypted(dto, "", crypto_ctx, source);
			crypto_ctx.inc_iv();

			if (i == 0) {
				is_tracking_mode = dto.spend_secret_key == SecretKey{};
			}
			else if ((is_tracking_mode && dto.spend_secret_key != SecretKey{}) ||
				(!is_tracking_mode && dto.spend_secret_key == SecretKey{})) {
				throw std::runtime_error("BAD_ADDRESS - All addresses must be whether tracking or not");
			}

			if (dto.spend_secret_key != SecretKey{}) {
				if (!keys_match(dto.spend_secret_key, dto.spend_public_key))
					throw std::runtime_error("Restored spend public key doesn't correspond to secret key");
			}
			else {
				if (!crypto::key_isvalid(dto.spend_public_key)) {
					throw std::runtime_error("WRONG_PASSWORD - Public spend key is incorrect");
				}
			}

			WalletRecord wallet;
			wallet.spend_public_key = dto.spend_public_key;
			wallet.spend_secret_key = dto.spend_secret_key;
			wallet.creation_timestamp = static_cast<Timestamp>(dto.creation_timestamp);

			index.push_back(wallet);
		}
	}

}  // namespace cryonerocoin
