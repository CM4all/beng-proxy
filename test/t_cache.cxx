/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cache.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "PInstance.hxx"

#include <gtest/gtest.h>

#include <time.h>

static void *
match_to_ptr(int match)
{
	return (void*)(long)match;
}

static int
ptr_to_match(void *p)
{
	return (int)(long)p;
}

struct MyCacheItem final : PoolHolder, CacheItem {
	const int match;
	const int value;

	MyCacheItem(PoolPtr &&_pool, int _match, int _value) noexcept
		:PoolHolder(std::move(_pool)),
		 CacheItem(std::chrono::steady_clock::now(),
			   std::chrono::hours(1), 1),
		 match(_match), value(_value) {
	}

	/* virtual methods from class CacheItem */
	void Destroy() noexcept override {
		this->~MyCacheItem();
	}
};

static MyCacheItem *
my_cache_item_new(struct pool *_pool, int match, int value)
{
	auto pool = pool_new_linear(_pool, "my_cache_item", 1024);
	auto i = NewFromPool<MyCacheItem>(std::move(pool), match, value);
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

	Cache cache(instance.event_loop, 1024, 4);

	/* add first item */

	i = my_cache_item_new(instance.root_pool, 1, 0);
	cache.Put("foo", *i);

	/* overwrite first item */

	i = my_cache_item_new(instance.root_pool, 2, 0);
	cache.Put("foo", *i);

	/* check overwrite result */

	i = (MyCacheItem *)cache.Get("foo");
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 0);

	i = (MyCacheItem *)cache.GetMatch("foo", my_match, match_to_ptr(1));
	ASSERT_EQ(i, nullptr);

	i = (MyCacheItem *)cache.GetMatch("foo", my_match, match_to_ptr(2));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 0);

	/* add new item */

	i = my_cache_item_new(instance.root_pool, 1, 1);
	cache.PutMatch("foo", *i, my_match, match_to_ptr(1));

	/* check second item */

	i = (MyCacheItem *)cache.GetMatch("foo", my_match, match_to_ptr(1));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 1);
	ASSERT_EQ(i->value, 1);

	/* check first item */

	i = (MyCacheItem *)cache.GetMatch("foo", my_match, match_to_ptr(2));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 0);

	/* overwrite first item */

	i = my_cache_item_new(instance.root_pool, 1, 3);
	cache.PutMatch("foo", *i, my_match, match_to_ptr(1));

	i = (MyCacheItem *)cache.GetMatch("foo", my_match, match_to_ptr(1));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 1);
	ASSERT_EQ(i->value, 3);

	i = (MyCacheItem *)cache.GetMatch("foo", my_match, match_to_ptr(2));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 0);

	/* overwrite second item */

	i = my_cache_item_new(instance.root_pool, 2, 4);
	cache.PutMatch("foo", *i, my_match, match_to_ptr(2));

	i = (MyCacheItem *)cache.GetMatch("foo", my_match, match_to_ptr(1));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 1);
	ASSERT_EQ(i->value, 3);

	i = (MyCacheItem *)cache.GetMatch("foo", my_match, match_to_ptr(2));
	ASSERT_NE(i, nullptr);
	ASSERT_EQ(i->match, 2);
	ASSERT_EQ(i->value, 4);
}
