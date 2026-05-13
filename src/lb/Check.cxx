// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Check.hxx"
#include "net/SocketAddress.hxx"

#include <algorithm> // for std::any_of()

#include <assert.h>
#include <sys/stat.h>

bool
LbHttpCheckConfig::MatchClientAddress(SocketAddress address) const noexcept
{
	if (client_addresses.empty())
		return true;

	return std::any_of(client_addresses.begin(), client_addresses.end(),
			   [=](const auto &i){
				   return i.Matches(address);
			   });
}

bool
LbHttpCheckConfig::Check() const noexcept
{
	assert(!file_exists.empty());

	struct stat st;
	return stat(file_exists.c_str(), &st) == 0;
}
