extern "C" {
#include "rubber.h"
}

#include <inline/compiler.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <assert.h>
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

static void
FillRubber(rubber *r, unsigned id, size_t length)
{
    Fill(rubber_write(r, id), length, id);
}

static unsigned
AddFillRubber(rubber *r, size_t length)
{
    unsigned id = rubber_add(r, length);
    if (id != 0)
        FillRubber(r, id, length);

    return id;
}

gcc_pure
static bool
CheckRubber(rubber *r, unsigned id, size_t length)
{
    return Check(rubber_read(r, id), length, id);
}

class RubberTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(RubberTest);
    CPPUNIT_TEST(TestBasic);
    CPPUNIT_TEST(TestShrink);
    CPPUNIT_TEST(TestFullTable);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestBasic() {
        size_t total = 4 * 1024 * 1024;

        rubber *r = rubber_new(total);
        CPPUNIT_ASSERT(r != NULL);

        total = rubber_get_max_size(r);

        /* fill the whole "rubber" object with four quarters */

        unsigned a = AddFillRubber(r, total / 4);
        CPPUNIT_ASSERT(a > 0);
        CPPUNIT_ASSERT_EQUAL(rubber_size_of(r, a), total / 4);

        unsigned b = AddFillRubber(r, total / 4);
        CPPUNIT_ASSERT(b > 0);
        CPPUNIT_ASSERT_EQUAL(rubber_size_of(r, b), total / 4);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total / 2);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total / 2);

        unsigned c = AddFillRubber(r, total / 4);
        CPPUNIT_ASSERT(c > 0);
        CPPUNIT_ASSERT_EQUAL(rubber_size_of(r, c), total / 4);

        unsigned d = AddFillRubber(r, total / 4);
        CPPUNIT_ASSERT(d > 0);
        CPPUNIT_ASSERT_EQUAL(rubber_size_of(r, d), total / 4);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total);

        /* another allocation must fail */

        CPPUNIT_ASSERT_EQUAL(AddFillRubber(r, 1), 0u);

        CPPUNIT_ASSERT(CheckRubber(r, a, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, b, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, c, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, d, total / 4));

        /* remove two non-adjacent allocations; the following
           rubber_add() call must automatically compress the "rubber"
           object, and the allocation succeeds */

        rubber_remove(r, b);
        rubber_remove(r, d);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total / 2);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total * 3 / 4);

        unsigned e = AddFillRubber(r, total / 2);
        CPPUNIT_ASSERT(e > 0);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total);

        CPPUNIT_ASSERT(CheckRubber(r, a, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, c, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, e, total / 2));

        /* remove one after another, and see if rubber results are
           correct */

        rubber_remove(r, a);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total * 3 / 4);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total);

        rubber_compress(r);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total * 3 / 4);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total * 3 / 4);
        CPPUNIT_ASSERT(CheckRubber(r, c, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, e, total / 2));

        rubber_remove(r, c);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total / 2);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total * 3 / 4);
        CPPUNIT_ASSERT(CheckRubber(r, e, total / 2));

        rubber_compress(r);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total / 2);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total / 2);
        CPPUNIT_ASSERT(CheckRubber(r, e, total / 2));

        rubber_remove(r, e);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), size_t(0u));
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), size_t(0u));

        rubber_compress(r);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), size_t(0u));
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), size_t(0u));

        rubber_free(r);
    }

    void TestShrink() {
        size_t total = 4 * 1024 * 1024;

        rubber *r = rubber_new(total);
        CPPUNIT_ASSERT(r != NULL);

        total = rubber_get_max_size(r);

        /* fill the whole "rubber" object */

        unsigned a = AddFillRubber(r, total * 3 / 4);
        CPPUNIT_ASSERT(a > 0);
        CPPUNIT_ASSERT_EQUAL(rubber_size_of(r, a), total * 3 / 4);

        unsigned b = AddFillRubber(r, total / 4);
        CPPUNIT_ASSERT(b > 0);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total);

        /* another allocation must fail */

        CPPUNIT_ASSERT_EQUAL(AddFillRubber(r, 1), 0u);

        CPPUNIT_ASSERT(CheckRubber(r, a, total * 3 / 4));
        CPPUNIT_ASSERT(CheckRubber(r, b, total / 4));

        /* shrink the first allocation, try again */

        rubber_shrink(r, a, total / 4);
        CPPUNIT_ASSERT_EQUAL(rubber_size_of(r, a), total / 4);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total / 2);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total);

        unsigned c = AddFillRubber(r, total / 2);
        CPPUNIT_ASSERT(c > 0);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total);

        CPPUNIT_ASSERT(CheckRubber(r, a, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, b, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, c, total / 2));

        /* shrink the third allocation, verify rubber_compress() */

        rubber_shrink(r, c, total / 4);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total * 3 / 4);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total);

        CPPUNIT_ASSERT(CheckRubber(r, a, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, b, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, c, total / 4));

        rubber_compress(r);

        CPPUNIT_ASSERT_EQUAL(rubber_get_netto_size(r), total * 3 / 4);
        CPPUNIT_ASSERT_EQUAL(rubber_get_brutto_size(r), total * 3 / 4);

        CPPUNIT_ASSERT(CheckRubber(r, a, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, b, total / 4));
        CPPUNIT_ASSERT(CheckRubber(r, c, total / 4));

        /* clean up */

        rubber_remove(r, a);
        rubber_remove(r, b);
        rubber_remove(r, c);
        rubber_free(r);
    }

    /**
     * Fill the allocation table, see if the allocator fails
     * eventually even though there's memory available.
     */
    void TestFullTable() {
        size_t total = 64 * 1024 * 1024;

        rubber *r = rubber_new(total);
        CPPUNIT_ASSERT(r != NULL);

        total = rubber_get_max_size(r);

        static const size_t max = 300000;
        static unsigned ids[max], n = 0;
        while (n < max) {
            unsigned id = rubber_add(r, 1);
            if (id == 0)
                break;

            CPPUNIT_ASSERT_EQUAL((size_t)rubber_read(r, id) % 0x10, size_t(0));

            ids[n++] = id;
        }

        CPPUNIT_ASSERT(n > 0);
        CPPUNIT_ASSERT(n < max);

        /* just to be sure: try again, must still fail */

        CPPUNIT_ASSERT_EQUAL(rubber_add(r, 1024 * 1024), 0u);

        /* remove one item; now a large allocation must succeed */

        rubber_remove(r, ids[n / 2]);

        unsigned id = rubber_add(r, 1024 * 1024);
        CPPUNIT_ASSERT(id > 0);
        CPPUNIT_ASSERT_EQUAL(id, ids[n / 2]);

        /* cleanup */

        for (unsigned i = 0; i < n; ++i)
            rubber_remove(r, ids[i]);

        rubber_free(r);
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(RubberTest);

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
