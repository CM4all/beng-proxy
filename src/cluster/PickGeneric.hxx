// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "PickFailover.hxx"
#include "PickModulo.hxx"
#include "StickyMode.hxx"
#include "RoundRobinBalancer.cxx"
#include "net/SocketAddress.hxx"
#include "time/Expiry.hxx"

/**
 * Pick an address using the given #StickyMode.
 */
template<typename List>
[[gnu::pure]]
const auto &
PickGeneric(Expiry now, StickyMode sticky_mode,
	    const List &list, sticky_hash_t sticky_hash) noexcept
{
	if (list.size() == 1)
		return *list.begin();

	switch (sticky_mode) {
	case StickyMode::NONE:
		break;

	case StickyMode::FAILOVER:
		return PickFailover(now, list);

	case StickyMode::SOURCE_IP:
	case StickyMode::HOST:
	case StickyMode::XHOST:
	case StickyMode::SESSION_MODULO:
	case StickyMode::COOKIE:
	case StickyMode::JVM_ROUTE:
		if (sticky_hash != 0)
			return PickModulo(now, list,
					  sticky_hash);

		break;
	}

	return list.GetRoundRobinBalancer().Get(now, list,
						sticky_mode == StickyMode::NONE);
}
