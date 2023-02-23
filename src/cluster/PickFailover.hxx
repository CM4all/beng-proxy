// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/Expiry.hxx"

#include <iterator>

#include <assert.h>

/**
 * Generic implementation of StickyMode::FAILOVER: pick the first
 * non-failing address.
 */
template<typename List>
[[gnu::pure]]
const auto &
PickFailover(Expiry now, const List &list) noexcept
{
	assert(std::begin(list) != std::end(list)); /* must not be empty */

	/* ignore "fade" status here */
	constexpr bool allow_fade = true;

	for (const auto &i : list)
		if (list.Check(now, i, allow_fade))
			return i;

	/* none available - return first node as last resort */
	return *std::begin(list);
}
