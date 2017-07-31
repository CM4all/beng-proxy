#include "pool.hxx"
#include "RootPool.hxx"
#include "util/Compiler.h"

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

    /* freeing doesn't reduce the size counters, because the pool
       doesn't know the size of the allocations that are being
       freed; these tests must be adjusted once this is
       implemented */
    p_free(pool, q);
    ASSERT_EQ(size_t(256 + 64), pool_brutto_size(pool));
    ASSERT_EQ(size_t(256 + 64), pool_netto_size(pool));
    p_free(pool, r);
    ASSERT_EQ(size_t(256 + 64), pool_brutto_size(pool));
    ASSERT_EQ(size_t(256 + 64), pool_netto_size(pool));
}

TEST(PoolTest, Linear)
{
    RootPool root_pool;
    LinearPool pool(root_pool, "foo", 64);
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
