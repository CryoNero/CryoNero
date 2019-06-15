// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The CryoNero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#pragma once

namespace platform 
{

	class PreventSleep
	{
	public:
		explicit PreventSleep(const char *reason);  // some OSes will show this string to user
		~PreventSleep();
	};
}
