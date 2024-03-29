// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#pragma once

#include <cstdint>
#include <vector>

#include "CryptoNote.hpp"
#include "common/Int128.hpp"

namespace cryonerocoin 
{
	bool check_hash(const crypto::Hash &hash, Difficulty difficulty);
	using CumulativeDifficulty = common::Uint128;
}
