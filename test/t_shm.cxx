#include "shm/shm.hxx"

#include <gtest/gtest.h>

TEST(ShmTest, Basic)
{
    struct shm *shm;
    void *a, *b, *c, *d, *e;

    shm = shm_new(1024, 2);

    a = shm_alloc(shm, 1);
    ASSERT_NE(a, nullptr);

    b = shm_alloc(shm, 2);
    ASSERT_EQ(b, nullptr);

    b = shm_alloc(shm, 1);
    ASSERT_NE(b, nullptr);

    c = shm_alloc(shm, 1);
    ASSERT_EQ(c, nullptr);

    shm_free(shm, a);
    c = shm_alloc(shm, 1);
    ASSERT_NE(c, nullptr);

    a = shm_alloc(shm, 1);
    ASSERT_EQ(a, nullptr);

    shm_free(shm, b);
    shm_free(shm, c);

    a = shm_alloc(shm, 2);
    ASSERT_NE(a, nullptr);

    b = shm_alloc(shm, 2);
    ASSERT_EQ(b, nullptr);

    b = shm_alloc(shm, 1);
    ASSERT_EQ(b, nullptr);

    shm_free(shm, a);

    a = shm_alloc(shm, 2);
    ASSERT_NE(a, nullptr);

    shm_close(shm);

    /* allocate and deallocate in different order, to see if adjacent
       free pages are merged properly */

    shm = shm_new(1024, 5);

    a = shm_alloc(shm, 1);
    ASSERT_NE(a, nullptr);

    b = shm_alloc(shm, 2);
    ASSERT_NE(b, nullptr);

    c = shm_alloc(shm, 1);
    ASSERT_NE(c, nullptr);

    d = shm_alloc(shm, 1);
    ASSERT_NE(d, nullptr);

    e = shm_alloc(shm, 1);
    ASSERT_EQ(e, nullptr);

    shm_free(shm, b);
    shm_free(shm, c);

    e = shm_alloc(shm, 4);
    ASSERT_EQ(e, nullptr);

    e = shm_alloc(shm, 3);
    ASSERT_NE(e, nullptr);
    shm_free(shm, e);

    b = shm_alloc(shm, 2);
    ASSERT_NE(b, nullptr);

    c = shm_alloc(shm, 1);
    ASSERT_NE(c, nullptr);

    shm_free(shm, c);
    shm_free(shm, b);

    e = shm_alloc(shm, 4);
    ASSERT_EQ(e, nullptr);

    e = shm_alloc(shm, 3);
    ASSERT_NE(e, nullptr);
    shm_free(shm, e);

    shm_close(shm);
}
