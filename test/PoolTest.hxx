#include "pool.hxx"

#include <cppunit/TestFixture.h>

#include <cstddef>

class PoolTest : public CppUnit::TestFixture {
    struct pool *root_pool, *the_pool;

protected:
    struct pool *GetPool() {
        return the_pool;
    }

public:
    virtual void setUp() {
        root_pool = pool_new_libc(NULL, "root");
        the_pool = pool_new_libc(root_pool, "test");
    }

    virtual void tearDown() {
        pool_unref(root_pool);
        pool_unref(the_pool);
        pool_commit();
        pool_recycler_clear();
    }
};
