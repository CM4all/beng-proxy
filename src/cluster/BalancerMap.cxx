// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "BalancerMap.hxx"

RoundRobinBalancer &
BalancerMap::MakeRoundRobinBalancer(HashKey key) noexcept
{
	auto *item = cache.Get(key);

	if (item == nullptr)
		/* create a new cache item */
		item = &cache.Put(std::move(key), RoundRobinBalancer());

	return *item;
}
