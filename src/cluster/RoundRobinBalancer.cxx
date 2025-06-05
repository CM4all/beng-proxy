// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "RoundRobinBalancer.hxx"
#include "time/Expiry.hxx"

#include <cassert>
#include <iterator>

template<typename List>
inline typename List::const_reference
RoundRobinBalancer::Next(const List &list) noexcept
{
	assert(list.size() >= 2);
	assert(next < list.size());

	const auto &address = *std::next(list.begin(), next);

	++next;
	if (next >= list.size())
		next = 0;

	return address;
}

template<typename List>
typename List::const_reference
RoundRobinBalancer::Get(const Expiry now,
			const List &list,
			bool allow_fade) noexcept
{
	const auto &first = Next(list);
	const auto *ret = &first;
	do {
		if (list.Check(now, *ret, allow_fade))
			return *ret;

		ret = &Next(list);
	} while (ret != &first);

	/* all addresses failed: */
	return first;
}
