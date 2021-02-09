/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
