#ifndef TEST_POOL_HXX
#define TEST_POOL_HXX

#include "pool.hxx"

class TestPool {
    struct pool *const root_pool, *the_pool;

public:
    TestPool()
        :root_pool(pool_new_libc(nullptr, "root")),
         the_pool(pool_new_libc(root_pool, "test")) {}

    ~TestPool() {
        pool_unref(root_pool);
        if (the_pool != nullptr)
            pool_unref(the_pool);
        pool_commit();
        pool_recycler_clear();
    }

    TestPool(const TestPool &) = delete;
    TestPool &operator=(const TestPool &) = delete;

    operator struct pool &() {
        assert(the_pool != nullptr);

        return *the_pool;
    }

    operator struct pool *() {
        assert(the_pool != nullptr);

        return the_pool;
    }

    struct pool &Steal() {
        assert(the_pool != nullptr);

        return *std::exchange(the_pool, nullptr);
    }
};

#endif
