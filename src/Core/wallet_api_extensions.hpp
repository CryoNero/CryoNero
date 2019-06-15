// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The CryoNero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.
#pragma once

#include "rpc_api.hpp"

using namespace crypto;

namespace cryonero
{
	namespace api
	{
		namespace extensions
		{
			struct GetKeys
			{
				static std::string method() { return "get_keys"; }

				struct Request
				{
					std::string address;
				};

				struct Response
				{
					BinaryArray keys;
				};
			};
		}
	}
}

namespace seria
{
	void ser_members(cryonero::api::extensions::GetKeys::Request &v, ISeria &s);
	void ser_members(cryonero::api::extensions::GetKeys::Response &v, ISeria &s);
}