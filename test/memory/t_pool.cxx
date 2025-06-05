// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "pool/RootPool.hxx"

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>

TEST(PoolTest, Libc)
{
	RootPool pool;
	ASSERT_EQ(size_t(0), pool_brutto_size(pool));
	ASSERT_EQ(size_t(0), pool_netto_size(pool));

	const void *q = p_malloc(pool, 64);
	ASSERT_NE(q, nullptr);
	ASSERT_EQ(size_t(64), pool_brutto_size(pool));
	ASSERT_EQ(size_t(64), pool_netto_size(pool));

	const void *r = p_malloc(pool, 256);
	ASSERT_NE(r, nullptr);
	ASSERT_EQ(size_t(256 + 64), pool_brutto_size(pool));
	ASSERT_EQ(size_t(256 + 64), pool_netto_size(pool));

	p_free(pool, q, 64);
	ASSERT_EQ(size_t(256), pool_brutto_size(pool));
	ASSERT_EQ(size_t(256), pool_netto_size(pool));
	p_free(pool, r, 256);
	ASSERT_EQ(size_t(0), pool_brutto_size(pool));
	ASSERT_EQ(size_t(0), pool_netto_size(pool));
}

TEST(PoolTest, Linear)
{
	RootPool root_pool;
	const auto pool = pool_new_linear(root_pool, "foo", 64);
#ifdef NDEBUG
	ASSERT_EQ(size_t(0), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(0), pool_netto_size(pool));

	const void *q = p_malloc(pool, 1024);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(1024), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(1024), pool_netto_size(pool));

	q = p_malloc(pool, 32);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(1024 + 64), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(1024 + 32), pool_netto_size(pool));

	q = p_malloc(pool, 16);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(1024 + 64), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(1024 + 32 + 16), pool_netto_size(pool));

	q = p_malloc(pool, 32);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(1024 + 2 * 64), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(1024 + 32 + 16 + 32), pool_netto_size(pool));

	q = p_malloc(pool, 1024);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(2 * 1024 + 2 * 64), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(2 * 1024 + 32 + 16 + 32), pool_netto_size(pool));
}
