#include "http_address.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

#include <assert.h>
#include <string.h>

TEST(HttpAddressTest, Unix)
{
    TestPool root_pool;
    AllocatorPtr alloc(root_pool);

    auto *a = http_address_parse(alloc, "unix:/var/run/foo");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->host_and_port, nullptr);
    ASSERT_STREQ(a->path, "/var/run/foo");
}

TEST(HttpAddressTest, Apply)
{
    TestPool root_pool;
    AllocatorPtr alloc(root_pool);

    auto *a = http_address_parse(alloc, "http://localhost/foo");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->protocol, HttpAddress::Protocol::HTTP);
    ASSERT_NE(a->host_and_port, nullptr);
    ASSERT_STREQ(a->host_and_port, "localhost");
    ASSERT_STREQ(a->path, "/foo");

    const auto *b = a->Apply(alloc, "");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->protocol, a->protocol);
    ASSERT_STREQ(b->host_and_port, a->host_and_port);
    ASSERT_STREQ(b->path, "/foo");

    b = a->Apply(alloc, "bar");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->protocol, a->protocol);
    ASSERT_STREQ(b->host_and_port, a->host_and_port);
    ASSERT_STREQ(b->path, "/bar");

    b = a->Apply(alloc, "/");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->protocol, a->protocol);
    ASSERT_STREQ(b->host_and_port, a->host_and_port);
    ASSERT_STREQ(b->path, "/");

    b = a->Apply(alloc, "http://example.com/");
    ASSERT_EQ(b, nullptr);

    b = a->Apply(alloc, "http://localhost/bar");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->protocol, a->protocol);
    ASSERT_STREQ(b->host_and_port, a->host_and_port);
    ASSERT_STREQ(b->path, "/bar");

    b = a->Apply(alloc, "?query");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->protocol, a->protocol);
    ASSERT_STREQ(b->host_and_port, a->host_and_port);
    ASSERT_STREQ(b->path, "/foo?query");
}
