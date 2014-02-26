#include "pool.h"

#include <inline/compiler.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

class PoolTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(PoolTest);
    CPPUNIT_TEST(TestLibc);
    CPPUNIT_TEST(TestLinear);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestLibc() {
        struct pool *pool = pool_new_libc(NULL, "foo");
        CPPUNIT_ASSERT(pool != NULL);
        CPPUNIT_ASSERT_EQUAL(size_t(0), pool_brutto_size(pool));
        CPPUNIT_ASSERT_EQUAL(size_t(0), pool_netto_size(pool));

        const void *q = p_malloc(pool, 64);
        CPPUNIT_ASSERT(q != NULL);
        CPPUNIT_ASSERT_EQUAL(size_t(64), pool_brutto_size(pool));
        CPPUNIT_ASSERT_EQUAL(size_t(64), pool_netto_size(pool));

        const void *r = p_malloc(pool, 256);
        CPPUNIT_ASSERT(r != NULL);
        CPPUNIT_ASSERT_EQUAL(size_t(256 + 64), pool_brutto_size(pool));
        CPPUNIT_ASSERT_EQUAL(size_t(256 + 64), pool_netto_size(pool));

        /* freeing doesn't reduce the size counters, because the pool
           doesn't know the size of the allocations that are being
           freed; these tests must be adjusted once this is
           implemented */
        p_free(pool, q);
        CPPUNIT_ASSERT_EQUAL(size_t(256 + 64), pool_brutto_size(pool));
        CPPUNIT_ASSERT_EQUAL(size_t(256 + 64), pool_netto_size(pool));
        p_free(pool, r);
        CPPUNIT_ASSERT_EQUAL(size_t(256 + 64), pool_brutto_size(pool));
        CPPUNIT_ASSERT_EQUAL(size_t(256 + 64), pool_netto_size(pool));

        pool_unref(pool);
    }

    void TestLinear() {
        struct pool *root = pool_new_libc(NULL, "root");
        struct pool *pool = pool_new_linear(root, "foo", 64);
        CPPUNIT_ASSERT(pool != NULL);
#ifdef NDEBUG
        CPPUNIT_ASSERT_EQUAL(size_t(0), pool_brutto_size(pool));
#endif
        CPPUNIT_ASSERT_EQUAL(size_t(0), pool_netto_size(pool));

        const void *q = p_malloc(pool, 1024);
        CPPUNIT_ASSERT(q != NULL);
#ifdef NDEBUG
        CPPUNIT_ASSERT_EQUAL(size_t(1024), pool_brutto_size(pool));
#endif
        CPPUNIT_ASSERT_EQUAL(size_t(1024), pool_netto_size(pool));

        q = p_malloc(pool, 32);
        CPPUNIT_ASSERT(q != NULL);
#ifdef NDEBUG
        CPPUNIT_ASSERT_EQUAL(size_t(1024 + 64), pool_brutto_size(pool));
#endif
        CPPUNIT_ASSERT_EQUAL(size_t(1024 + 32), pool_netto_size(pool));

        q = p_malloc(pool, 16);
        CPPUNIT_ASSERT(q != NULL);
#ifdef NDEBUG
        CPPUNIT_ASSERT_EQUAL(size_t(1024 + 64), pool_brutto_size(pool));
#endif
        CPPUNIT_ASSERT_EQUAL(size_t(1024 + 32 + 16), pool_netto_size(pool));

        q = p_malloc(pool, 32);
        CPPUNIT_ASSERT(q != NULL);
#ifdef NDEBUG
        CPPUNIT_ASSERT_EQUAL(size_t(1024 + 2 * 64), pool_brutto_size(pool));
#endif
        CPPUNIT_ASSERT_EQUAL(size_t(1024 + 32 + 16 + 32), pool_netto_size(pool));

        q = p_malloc(pool, 1024);
        CPPUNIT_ASSERT(q != NULL);
#ifdef NDEBUG
        CPPUNIT_ASSERT_EQUAL(size_t(2 * 1024 + 2 * 64), pool_brutto_size(pool));
#endif
        CPPUNIT_ASSERT_EQUAL(size_t(2 * 1024 + 32 + 16 + 32), pool_netto_size(pool));

        pool_unref(pool);
        pool_unref(root);
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(PoolTest);

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    CppUnit::Test *suite =
        CppUnit::TestFactoryRegistry::getRegistry().makeTest();

    CppUnit::TextUi::TestRunner runner;
    runner.addTest(suite);

    runner.setOutputter(new CppUnit::CompilerOutputter(&runner.result(),
                                                       std::cerr));
    bool success = runner.run();

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
