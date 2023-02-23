// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ConditionConfig.hxx"
#include "net/IPv4Address.hxx"

bool
LbConditionConfig::MatchAddress(SocketAddress address) const noexcept
{
	IPv4Address ipv4;
	if (address.IsV4Mapped()) {
		ipv4 = address.UnmapV4();
		address = ipv4;
	}

	return std::get<MaskedSocketAddress>(value).Matches(address);
}
