#include "expansible_buffer.hxx"
#include "TestPool.hxx"
#include "util/ConstBuffer.hxx"

#include <gtest/gtest.h>

#include <string.h>

TEST(ExpansibleBufferTest, Basic)
{
    TestPool pool;

    ExpansibleBuffer eb(pool, 4, 1024);
    ASSERT_TRUE(eb.IsEmpty());

    auto p = eb.Read();
    ASSERT_NE(p.data, nullptr);
    ASSERT_EQ(p.size, 0);

    eb.Write("01");
    ASSERT_FALSE(eb.IsEmpty());

    auto q = eb.Read();
    ASSERT_EQ(q.data, p.data);
    ASSERT_EQ(q.size, 2);
    ASSERT_EQ(memcmp(q.data, "01", 2), 0);

    eb.Write("234");
    ASSERT_FALSE(eb.IsEmpty());

    q = eb.Read();
    ASSERT_NE(q.data, p.data);
    ASSERT_EQ(q.size, 5);
    ASSERT_EQ(memcmp(q.data, "01234", 5), 0);

    eb.Clear();
    ASSERT_TRUE(eb.IsEmpty());

    p = eb.Read();
    ASSERT_EQ(p.data, q.data);
    ASSERT_EQ(p.size, 0);

    eb.Write("abcdef");
    ASSERT_FALSE(eb.IsEmpty());

    p = eb.Read();
    ASSERT_EQ(p.data, q.data);
    ASSERT_EQ(p.size, 6);
    ASSERT_EQ(memcmp(q.data, "abcdef", 6), 0);

    void *r = eb.Write(512);
    ASSERT_NE(r, nullptr);

    /* this call hits the hard limit */
    r = eb.Write(512);
    ASSERT_EQ(r, nullptr);
}
