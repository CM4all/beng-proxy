#include "SlicePool.hxx"

#include <inline/compiler.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <stdint.h>
#include <stdlib.h>

static void
Fill(void *_p, size_t length, unsigned seed)
{
    for (uint8_t *p = (uint8_t *)_p, *end = p + length; p != end; ++p)
        *p = (uint8_t)seed++;
}

gcc_pure
static bool
Check(const void *_p, size_t length, unsigned seed)
{
    for (const uint8_t *p = (const uint8_t *)_p, *end = p + length;
         p != end; ++p)
        if (*p != (uint8_t)seed++)
            return false;

    return true;
}

class SliceTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(SliceTest);
    CPPUNIT_TEST(TestSmall);
    CPPUNIT_TEST(TestMedium);
    CPPUNIT_TEST(TestLarge);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestSmall() {
        const size_t slice_size = 13;
        const unsigned per_area = 600;

        auto *pool = slice_pool_new(slice_size, per_area);
        CPPUNIT_ASSERT(pool != NULL);

        auto allocation0 = slice_alloc(pool);
        auto *area0 = allocation0.area;
        CPPUNIT_ASSERT(area0 != NULL);
        slice_free(pool, area0, allocation0.data);

        void *allocations[per_area];

        for (unsigned i = 0; i < per_area; ++i) {
            auto allocation = slice_alloc(pool);
            CPPUNIT_ASSERT_EQUAL(allocation.area, area0);

            allocations[i] = allocation.data;
            CPPUNIT_ASSERT(allocations[i] != NULL);
            CPPUNIT_ASSERT(i <= 0 || allocations[i] != allocations[0]);
            CPPUNIT_ASSERT(i <= 1 || allocations[i] != allocations[1]);
            CPPUNIT_ASSERT(i <= 128 || allocations[i] != allocations[128]);

            Fill(allocations[i], slice_size, i);
        }

        struct {
            SliceArea *area;
            void *p;
        } more[per_area];

        for (unsigned i = 0; i < per_area; ++i) {
            auto allocation = slice_alloc(pool);

            more[i].area = allocation.area;
            more[i].p = allocation.data;
            CPPUNIT_ASSERT(more[i].p != NULL);

            Fill(more[i].p, slice_size, per_area + i);
        }

        CPPUNIT_ASSERT(more[per_area - 1].area != area0);

        for (unsigned i = 0; i < per_area; ++i) {
            CPPUNIT_ASSERT(Check(allocations[i], slice_size, i));
            slice_free(pool, area0, allocations[i]);

            CPPUNIT_ASSERT(Check(more[i].p, slice_size, per_area + i));
            slice_free(pool, more[i].area, more[i].p);
        }

        slice_pool_free(pool);
    }

    void TestMedium() {
        const size_t slice_size = 3000;
        const unsigned per_area = 10;

        auto *pool = slice_pool_new(slice_size, per_area);
        CPPUNIT_ASSERT(pool != NULL);

        auto allocation0 = slice_alloc(pool);
        auto *area0 = allocation0.area;
        CPPUNIT_ASSERT(area0 != NULL);
        slice_free(pool, area0, allocation0.data);

        void *allocations[per_area];

        for (unsigned i = 0; i < per_area; ++i) {
            auto allocation = slice_alloc(pool);
            CPPUNIT_ASSERT_EQUAL(allocation.area, area0);

            allocations[i] = allocation.data;
            CPPUNIT_ASSERT(allocations[i] != NULL);
            CPPUNIT_ASSERT(i <= 0 || allocations[i] != allocations[0]);
            CPPUNIT_ASSERT(i <= 1 || allocations[i] != allocations[1]);
            CPPUNIT_ASSERT(i <= per_area - 1 ||
                           allocations[i] != allocations[per_area - 1]);

            Fill(allocations[i], slice_size, i);
        }

        struct {
            SliceArea *area;
            void *p;
        } more[per_area];

        for (unsigned i = 0; i < per_area; ++i) {
            auto allocation = slice_alloc(pool);

            more[i].area = allocation.area;
            more[i].p = allocation.data;
            CPPUNIT_ASSERT(more[i].p != NULL);

            Fill(more[i].p, slice_size, per_area + i);
        }

        for (unsigned i = 0; i < per_area; ++i) {
            CPPUNIT_ASSERT(Check(allocations[i], slice_size, i));
            slice_free(pool, area0, allocations[i]);

            CPPUNIT_ASSERT(Check(more[i].p, slice_size, per_area + i));
            slice_free(pool, more[i].area, more[i].p);
        }

        slice_pool_free(pool);
    }

    void TestLarge() {
        const size_t slice_size = 8192;
        const unsigned per_area = 13;

        auto *pool = slice_pool_new(slice_size, per_area);
        CPPUNIT_ASSERT(pool != NULL);

        auto allocation0 = slice_alloc(pool);
        auto *area0 = allocation0.area;
        CPPUNIT_ASSERT(area0 != NULL);
        slice_free(pool, area0, allocation0.data);

        void *allocations[per_area];

        for (unsigned i = 0; i < per_area; ++i) {
            auto allocation = slice_alloc(pool);
            CPPUNIT_ASSERT_EQUAL(allocation.area, area0);

            allocations[i] = allocation.data;
            CPPUNIT_ASSERT(allocations[i] != NULL);
            CPPUNIT_ASSERT(i <= 0 || allocations[i] != allocations[0]);
            CPPUNIT_ASSERT(i <= 1 || allocations[i] != allocations[1]);
            CPPUNIT_ASSERT(i <= per_area - 1 ||
                           allocations[i] != allocations[per_area - 1]);

            Fill(allocations[i], slice_size, i);
        }

        struct {
            SliceArea *area;
            void *p;
        } more[per_area];

        for (unsigned i = 0; i < per_area; ++i) {
            auto allocation = slice_alloc(pool);

            more[i].area = allocation.area;
            more[i].p = allocation.data;
            CPPUNIT_ASSERT(more[i].p != NULL);

            Fill(more[i].p, slice_size, per_area + i);
        }

        for (unsigned i = 0; i < per_area; ++i) {
            CPPUNIT_ASSERT(Check(allocations[i], slice_size, i));
            slice_free(pool, area0, allocations[i]);

            CPPUNIT_ASSERT(Check(more[i].p, slice_size, per_area + i));
            slice_free(pool, more[i].area, more[i].p);
        }

        slice_pool_free(pool);
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SliceTest);

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
