// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ParseBareInetAddress.hxx"
#include "net/BareInetAddress.hxx"
#include "util/StringSplit.hxx"

#include <algorithm> // for std::copy()

bool
ParseBareInetAddress(BareInetAddress &address, std::string_view _s) noexcept
{
	const auto [s, scope_id] = Split(_s, '%');

	char buffer[64];
	if (s.size() >= sizeof(buffer))
		return false;

	*std::copy(s.begin(), s.end(), buffer) = '\0';
	return address.Parse(buffer);
}
