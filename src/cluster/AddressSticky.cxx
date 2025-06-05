// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AddressSticky.hxx"
#include "net/SocketAddress.hxx"
#include "util/djb_hash.hxx"

sticky_hash_t
socket_address_sticky(SocketAddress address) noexcept
{
	const auto p = address.GetSteadyPart();
	if (p.empty())
		return 0;

	return djb_hash(p);
}
