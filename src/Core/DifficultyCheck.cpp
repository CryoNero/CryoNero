// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The CryoNero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include "DifficultyCheck.hpp"

static const cryonero::DifficultyCheck table[1] = {
    {UINT32_MAX, "", 0},
    };

const cryonero::DifficultyCheck *cryonero::difficulty_check = table;
const size_t cryonero::difficulty_check_count               = sizeof(table) / sizeof(*table);
