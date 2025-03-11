// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "StickyHash.hxx"
#include "time/Expiry.hxx"

#include <iterator>

#include <assert.h>

/**
 * Pick an address using a #sticky_hash_t module the list size.  If
 * that address is failed, pick the next one.
 */
template<typename List>
[[gnu::pure]]
const auto &
PickModulo(Expiry now, const List &list, sticky_hash_t sticky_hash) noexcept
{
	const size_t n = std::size(list);
	assert(n >= 2);

	const size_t modulo = sticky_hash % n;
	bool allow_fade = true;

	const auto selected = std::next(std::begin(list), modulo);
	const auto end = std::end(list);

	auto i = selected;
	do {
		if (list.Check(now, *i, allow_fade))
			break;

		/* only the first iteration is allowed to override
		   FAILURE_FADE */
		allow_fade = false;

		++i;
		if (i == end)
			i = std::begin(list);
	} while (i != selected);

	/* all addresses failed: */
	return *i;
}
