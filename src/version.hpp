// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The CryoNero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#pragma once

#define cryonero_VERSION_WINDOWS_COMMA 2, 18, 8, 31
#define cryonero_VERSION_STRING "2.1.5.8.31"

#ifndef RC_INVOKED 

namespace cryonero
{
	inline const char *app_version() { return cryonero_VERSION_STRING; }
}

#endif
