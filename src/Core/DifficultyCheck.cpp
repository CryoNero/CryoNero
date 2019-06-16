// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include "DifficultyCheck.hpp"

static const cryonerocoin::DifficultyCheck table[1] = {
    {UINT32_MAX, "", 0},
    };

const cryonerocoin::DifficultyCheck *cryonerocoin::difficulty_check = table;
const size_t cryonerocoin::difficulty_check_count               = sizeof(table) / sizeof(*table);
