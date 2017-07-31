#include "shm/shm.hxx"
#include "shm/dpool.hxx"

#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <string.h>

TEST(ShmTest, Dpool)
{
    void *a, *b, *c, *d;

    auto *shm = shm_new(1024, 2);
    ASSERT_NE(shm, nullptr);

    auto *pool = dpool_new(*shm);
    ASSERT_NE(pool, nullptr);

    a = shm_alloc(shm, 1);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(a, pool);

    b = shm_alloc(shm, 1);
    ASSERT_EQ(b, nullptr);

    shm_free(shm, a);

    a = d_malloc(*pool, 512);
    ASSERT_NE(a, nullptr);
    memset(a, 0, 512);

    b = d_malloc(*pool, 800);
    ASSERT_NE(b, nullptr);
    memset(b, 0, 800);

    try {
        c = d_malloc(*pool, 512);
        ASSERT_EQ(c, nullptr);
    } catch (std::bad_alloc) {
    }

    d = d_malloc(*pool, 220);
    ASSERT_NE(d, nullptr);

    d_free(*pool, a);

    a = d_malloc(*pool, 240);
    ASSERT_NE(a, nullptr);

    try {
        c = d_malloc(*pool, 270);
        ASSERT_EQ(c, nullptr);
    } catch (std::bad_alloc) {
    }

    /* no free SHM page */
    c = shm_alloc(shm, 1);
    ASSERT_EQ(c, nullptr);

    /* free "b" which should release one SHM page */
    d_free(*pool, b);

    c = shm_alloc(shm, 1);
    ASSERT_NE(c, nullptr);

    dpool_destroy(pool);
    shm_close(shm);
}
