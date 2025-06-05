// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "cache/Cache.hxx"
#include "cache/Item.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "PInstance.hxx"

#include <gtest/gtest.h>

#include <time.h>

static void *
match_to_ptr(int match)
{
	return (void*)(intptr_t)match;
}

static int
ptr_to_match(void *p)
{
	return (int)(intptr_t)p;
}

struct MyCacheItem final : PoolHolder, CacheItem {
	const int match;
	const int value;

	MyCacheItem(PoolPtr &&_pool, StringWithHash _key, int _match, int _value) noexcept
		:PoolHolder(std::move(_pool)),
		 CacheItem(_key, 1, std::chrono::steady_clock::now(),
			   std::chrono::hours(1)),
		 match(_match), value(_value) {
	}

	/* virtual methods from class CacheItem */
	void Destroy() noexcept override {
		this->~MyCacheItem();
	}
};

static MyCacheItem *
my_cache_item_new(struct pool *_pool, StringWithHash key, int match, int value)
{
	auto pool = pool_new_linear(_pool, "my_cache_item", 1024);
	auto i = NewFromPool<MyCacheItem>(std::move(pool), key, match, value);
	return i;
}

static bool
my_match(const CacheItem *item, void *ctx)
{
	const MyCacheItem *i = (const MyCacheItem *)item;
	int match = ptr_to_match(ctx);

	return i->match == match;
}

TEST(TranslationCache, Basic)
{
	MyCacheItem *i;

	PInstance instance;

	Cache cache(instance.event_loop, 4);

	const StringWithHash foo_key{"foo"};

	/* add first item */

	i = my_cache_item_new(instance.root_pool, foo_key, 1, 0);
	cache.Put(*i);

	/* overwrite first item */

	i = my_cache_item_new(instance.root_pool, foo_key, 2, 0);
	cache.Put(*i);

	/* check overwrite result */

	i = (MyCacheItem *)cache.Get(foo_key);
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 0);

	i = (MyCacheItem *)cache.GetMatch(foo_key, my_match, match_to_ptr(1));
	ASSERT_EQ(i, nullptr);

	i = (MyCacheItem *)cache.GetMatch(foo_key, my_match, match_to_ptr(2));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 0);

	/* add new item */

	i = my_cache_item_new(instance.root_pool, foo_key, 1, 1);
	cache.PutMatch(*i, my_match, match_to_ptr(1));

	/* check second item */

	i = (MyCacheItem *)cache.GetMatch(foo_key, my_match, match_to_ptr(1));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 1);
	ASSERT_EQ(i->value, 1);

	/* check first item */

	i = (MyCacheItem *)cache.GetMatch(foo_key, my_match, match_to_ptr(2));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 0);

	/* overwrite first item */

	i = my_cache_item_new(instance.root_pool, foo_key, 1, 3);
	cache.PutMatch(*i, my_match, match_to_ptr(1));

	i = (MyCacheItem *)cache.GetMatch(foo_key, my_match, match_to_ptr(1));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 1);
	ASSERT_EQ(i->value, 3);

	i = (MyCacheItem *)cache.GetMatch(foo_key, my_match, match_to_ptr(2));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 0);

	/* overwrite second item */

	i = my_cache_item_new(instance.root_pool, foo_key, 2, 4);
	cache.PutMatch(*i, my_match, match_to_ptr(2));

	i = (MyCacheItem *)cache.GetMatch(foo_key, my_match, match_to_ptr(1));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 1);
	ASSERT_EQ(i->value, 3);

	i = (MyCacheItem *)cache.GetMatch(foo_key, my_match, match_to_ptr(2));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 4);
}
