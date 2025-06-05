// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Cache.hxx"
#include "pool/pool.hxx"
#include "AllocatorPtr.hxx"

inline
WidgetClassCache::Item::Item(PoolPtr &&_pool, const WidgetClass &_cls) noexcept
	:PoolHolder(std::move(_pool)), cls(AllocatorPtr(pool), _cls)
{
}

WidgetClassCache::WidgetClassCache(struct pool &parent_pool) noexcept
	:PoolHolder(pool_new_dummy(&parent_pool, "WidgetClassCache")) {}

void
WidgetClassCache::Put(const char *name, const WidgetClass &cls) noexcept
{
	auto item_pool = pool_new_linear(pool, "WidgetClassCacheItem", 4096);

	/* move the string to the new pool */
	name = p_strdup(item_pool, name);

	map.emplace(std::piecewise_construct,
		    std::forward_as_tuple(name),
		    std::forward_as_tuple(std::move(item_pool), cls));
}
