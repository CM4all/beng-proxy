#include "rubber.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

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
FillRubber(Rubber *r, unsigned id, size_t length)
{
    Fill(rubber_write(r, id), length, id);
}

static unsigned
AddFillRubber(Rubber *r, size_t length)
{
    unsigned id = rubber_add(r, length);
    if (id != 0)
        FillRubber(r, id, length);

    return id;
}

gcc_pure
static bool
CheckRubber(Rubber *r, unsigned id, size_t length)
{
    return Check(rubber_read(r, id), length, id);
}

TEST(RubberTest, Basic)
{
    size_t total = 4 * 1024 * 1024;

    Rubber *r = rubber_new(total);
    ASSERT_NE(r, nullptr);

    total = rubber_get_max_size(r);

    /* fill the whole "rubber" object with four quarters */

    unsigned a = AddFillRubber(r, total / 4);
    ASSERT_GT(a, 0);
    ASSERT_EQ(rubber_size_of(r, a), total / 4);

    unsigned b = AddFillRubber(r, total / 4);
    ASSERT_GT(b, 0);
    ASSERT_EQ(rubber_size_of(r, b), total / 4);

    ASSERT_EQ(rubber_get_netto_size(r), total / 2);
    ASSERT_EQ(rubber_get_brutto_size(r), total / 2);

    unsigned c = AddFillRubber(r, total / 4);
    ASSERT_GT(c, 0);
    ASSERT_EQ(rubber_size_of(r, c), total / 4);

    unsigned d = AddFillRubber(r, total / 4);
    ASSERT_GT(d, 0);
    ASSERT_EQ(rubber_size_of(r, d), total / 4);

    ASSERT_EQ(rubber_get_netto_size(r), total);
    ASSERT_EQ(rubber_get_brutto_size(r), total);

    /* another allocation must fail */

    ASSERT_EQ(AddFillRubber(r, 1), 0u);

    ASSERT_TRUE(CheckRubber(r, a, total / 4));
    ASSERT_TRUE(CheckRubber(r, b, total / 4));
    ASSERT_TRUE(CheckRubber(r, c, total / 4));
    ASSERT_TRUE(CheckRubber(r, d, total / 4));

    /* remove two non-adjacent allocations; the following
       rubber_add() call must automatically compress the "rubber"
       object, and the allocation succeeds */

    rubber_remove(r, b);
    rubber_remove(r, d);

    ASSERT_EQ(rubber_get_netto_size(r), total / 2);
    ASSERT_EQ(rubber_get_brutto_size(r), total * 3 / 4);

    unsigned e = AddFillRubber(r, total / 2);
    ASSERT_GT(e, 0);

    ASSERT_EQ(rubber_get_netto_size(r), total);
    ASSERT_EQ(rubber_get_brutto_size(r), total);

    ASSERT_TRUE(CheckRubber(r, a, total / 4));
    ASSERT_TRUE(CheckRubber(r, c, total / 4));
    ASSERT_TRUE(CheckRubber(r, e, total / 2));

    /* remove one after another, and see if rubber results are
       correct */

    rubber_remove(r, a);

    ASSERT_EQ(rubber_get_netto_size(r), total * 3 / 4);
    ASSERT_EQ(rubber_get_brutto_size(r), total);

    rubber_compress(r);

    ASSERT_EQ(rubber_get_netto_size(r), total * 3 / 4);
    ASSERT_EQ(rubber_get_brutto_size(r), total * 3 / 4);
    ASSERT_TRUE(CheckRubber(r, c, total / 4));
    ASSERT_TRUE(CheckRubber(r, e, total / 2));

    rubber_remove(r, c);

    ASSERT_EQ(rubber_get_netto_size(r), total / 2);
    ASSERT_EQ(rubber_get_brutto_size(r), total * 3 / 4);
    ASSERT_TRUE(CheckRubber(r, e, total / 2));

    rubber_compress(r);

    ASSERT_EQ(rubber_get_netto_size(r), total / 2);
    ASSERT_EQ(rubber_get_brutto_size(r), total / 2);
    ASSERT_TRUE(CheckRubber(r, e, total / 2));

    rubber_remove(r, e);

    ASSERT_EQ(rubber_get_netto_size(r), size_t(0u));
    ASSERT_EQ(rubber_get_brutto_size(r), size_t(0u));

    rubber_compress(r);

    ASSERT_EQ(rubber_get_netto_size(r), size_t(0u));
    ASSERT_EQ(rubber_get_brutto_size(r), size_t(0u));

    rubber_free(r);
}

TEST(RubberTest, Shrink)
{
    size_t total = 4 * 1024 * 1024;

    Rubber *r = rubber_new(total);
    ASSERT_NE(r, nullptr);

    total = rubber_get_max_size(r);

    /* fill the whole "rubber" object */

    unsigned a = AddFillRubber(r, total * 3 / 4);
    ASSERT_GT(a, 0);
    ASSERT_EQ(rubber_size_of(r, a), total * 3 / 4);

    unsigned b = AddFillRubber(r, total / 4);
    ASSERT_GT(b, 0);

    ASSERT_EQ(rubber_get_netto_size(r), total);
    ASSERT_EQ(rubber_get_brutto_size(r), total);

    /* another allocation must fail */

    ASSERT_EQ(AddFillRubber(r, 1), 0u);

    ASSERT_TRUE(CheckRubber(r, a, total * 3 / 4));
    ASSERT_TRUE(CheckRubber(r, b, total / 4));

    /* shrink the first allocation, try again */

    rubber_shrink(r, a, total / 4);
    ASSERT_EQ(rubber_size_of(r, a), total / 4);

    ASSERT_EQ(rubber_get_netto_size(r), total / 2);
    ASSERT_EQ(rubber_get_brutto_size(r), total);

    unsigned c = AddFillRubber(r, total / 2);
    ASSERT_GT(c, 0);

    ASSERT_EQ(rubber_get_netto_size(r), total);
    ASSERT_EQ(rubber_get_brutto_size(r), total);

    ASSERT_TRUE(CheckRubber(r, a, total / 4));
    ASSERT_TRUE(CheckRubber(r, b, total / 4));
    ASSERT_TRUE(CheckRubber(r, c, total / 2));

    /* shrink the third allocation, verify rubber_compress() */

    rubber_shrink(r, c, total / 4);

    ASSERT_EQ(rubber_get_netto_size(r), total * 3 / 4);
    ASSERT_EQ(rubber_get_brutto_size(r), total);

    ASSERT_TRUE(CheckRubber(r, a, total / 4));
    ASSERT_TRUE(CheckRubber(r, b, total / 4));
    ASSERT_TRUE(CheckRubber(r, c, total / 4));

    rubber_compress(r);

    ASSERT_EQ(rubber_get_netto_size(r), total * 3 / 4);
    ASSERT_EQ(rubber_get_brutto_size(r), total * 3 / 4);

    ASSERT_TRUE(CheckRubber(r, a, total / 4));
    ASSERT_TRUE(CheckRubber(r, b, total / 4));
    ASSERT_TRUE(CheckRubber(r, c, total / 4));

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
TEST(RubberTest, FullTable)
{
    size_t total = 64 * 1024 * 1024;

    Rubber *r = rubber_new(total);
    ASSERT_NE(r, nullptr);

    total = rubber_get_max_size(r);

    static const size_t max = 300000;
    static unsigned ids[max], n = 0;
    while (n < max) {
        unsigned id = rubber_add(r, 1);
        if (id == 0)
            break;

        ASSERT_EQ((size_t)rubber_read(r, id) % 0x10, size_t(0));

        ids[n++] = id;
    }

    ASSERT_GT(n, 0);
    ASSERT_LT(n, max);

    /* just to be sure: try again, must still fail */

    ASSERT_EQ(rubber_add(r, 1024 * 1024), 0u);

    /* remove one item; now a large allocation must succeed */

    rubber_remove(r, ids[n / 2]);

    unsigned id = rubber_add(r, 1024 * 1024);
    ASSERT_GT(id, 0);
    ASSERT_EQ(id, ids[n / 2]);

    /* cleanup */

    for (unsigned i = 0; i < n; ++i)
        rubber_remove(r, ids[i]);

    rubber_free(r);
}
