// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.


#pragma once

#include <cstdint>
#include <string>
#include "common/CommandLine.hpp"

namespace cryonerocoin {

	struct MiningConfig {
		explicit MiningConfig(common::CommandLine &cmd);

		std::string mining_address;
		std::string cryonerod_ip= std::move("127.0.0.1");
		uint16_t cryonerod_port = RPC_DEFAULT_PORT;
		size_t thread_count = std::thread::hardware_concurrency();

		size_t blocks_limit = 0;  
	};

}  // namespace cryonerocoin
