#include "pool.h"

#include <cppunit/TestFixture.h>

#include <cstddef>

class PoolTest : public CppUnit::TestFixture {
    struct pool *root_pool, *pool;

protected:
    struct pool *GetPool() {
        return pool;
    }

public:
    virtual void setUp() {
        root_pool = pool_new_libc(NULL, "root");
        pool = pool_new_libc(root_pool, "test");
    }

    virtual void tearDown() {
        pool_unref(root_pool);
        pool_unref(pool);
        pool_commit();
        pool_recycler_clear();
    }
};
