// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#pragma once

namespace common
{

	class Nocopy
	{
		Nocopy(const Nocopy &) = delete;
		Nocopy &operator=(const Nocopy &) = delete;
		Nocopy(const Nocopy &&) = delete;
		Nocopy &operator=(Nocopy &&) = delete;

	public:
		Nocopy() = default;
	};
}
