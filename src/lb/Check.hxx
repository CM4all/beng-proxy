// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "net/MaskedSocketAddress.hxx"

#include <algorithm>
#include <string>
#include <forward_list>

struct LbHttpCheckConfig {
	std::string host;
	std::string uri;
	std::string file_exists;
	std::string success_message;

	std::forward_list<MaskedSocketAddress> client_addresses;

	[[gnu::pure]]
	bool MatchClientAddress(SocketAddress address) const noexcept {
		if (client_addresses.empty())
			return true;

		return std::any_of(client_addresses.begin(), client_addresses.end(),
				   [=](const MaskedSocketAddress &i){
					   return i.Matches(address);
				   });
	}

	[[gnu::pure]]
	bool Match(const char *request_uri,
		   const char *request_host) const noexcept {
		return request_host != nullptr &&
			request_host == host && request_uri == uri;
	}

	[[gnu::pure]]
	bool Check() const noexcept;
};
