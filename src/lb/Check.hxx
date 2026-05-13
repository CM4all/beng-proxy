// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "net/MaskedInetAddress.hxx"

#include <string>
#include <vector>

class SocketAddress;

struct LbHttpCheckConfig {
	std::string host;
	std::string uri;
	std::string file_exists;
	std::string success_message;

	std::vector<MaskedInetAddress> client_addresses;

	[[gnu::pure]]
	bool MatchClientAddress(SocketAddress address) const noexcept;

	[[gnu::pure]]
	bool Match(const char *request_uri,
		   const char *request_host) const noexcept {
		return request_host != nullptr &&
			request_host == host && request_uri == uri;
	}

	[[gnu::pure]]
	bool Check() const noexcept;
};
