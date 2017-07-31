#include "cgi_address.hxx"
#include "TestPool.hxx"

#include <gtest/gtest.h>

TEST(CgiAddressTest, Uri)
{
    TestPool pool;

    CgiAddress a("/usr/bin/cgi");
    ASSERT_EQ(false, a.IsExpandable());
    ASSERT_STREQ(a.GetURI(pool), "/");

    a.script_name = "/";
    ASSERT_STREQ(a.GetURI(pool), "/");

    a.path_info = "foo";
    ASSERT_STREQ(a.GetURI(pool), "/foo");

    a.query_string = "";
    ASSERT_STREQ(a.GetURI(pool), "/foo?");

    a.query_string = "a=b";
    ASSERT_STREQ(a.GetURI(pool), "/foo?a=b");

    a.path_info = "";
    ASSERT_STREQ(a.GetURI(pool), "/?a=b");

    a.path_info = nullptr;
    ASSERT_STREQ(a.GetURI(pool), "/?a=b");

    a.script_name = "/test.cgi";
    a.path_info = nullptr;
    a.query_string = nullptr;
    ASSERT_STREQ(a.GetURI(pool), "/test.cgi");

    a.path_info = "/foo";
    ASSERT_STREQ(a.GetURI(pool), "/test.cgi/foo");

    a.script_name = "/bar/";
    ASSERT_STREQ(a.GetURI(pool), "/bar/foo");

    a.script_name = "/";
    ASSERT_STREQ(a.GetURI(pool), "/foo");

    a.script_name = nullptr;
    ASSERT_STREQ(a.GetURI(pool), "/foo");
}

TEST(CgiAddressTest, Apply)
{
    TestPool pool;

    CgiAddress a("/usr/bin/cgi");
    a.script_name = "/test.pl";
    a.path_info = "/foo";

    auto b = a.Apply(pool, "");
    ASSERT_EQ((const CgiAddress *)&a, b);

    b = a.Apply(pool, "bar");
    ASSERT_NE(b, nullptr);
    ASSERT_NE(b, &a);
    ASSERT_EQ(false, b->IsValidBase());
    ASSERT_STREQ(b->path, a.path);
    ASSERT_STREQ(b->script_name, a.script_name);
    ASSERT_STREQ(b->path_info, "/bar");

    a.path_info = "/foo/";
    ASSERT_EQ(true, a.IsValidBase());

    b = a.Apply(pool, "bar");
    ASSERT_NE(b, nullptr);
    ASSERT_NE(b, &a);
    ASSERT_EQ(false, b->IsValidBase());
    ASSERT_STREQ(b->path, a.path);
    ASSERT_STREQ(b->script_name, a.script_name);
    ASSERT_STREQ(b->path_info, "/foo/bar");

    b = a.Apply(pool, "/bar");
    ASSERT_NE(b, nullptr);
    ASSERT_NE(b, &a);
    ASSERT_EQ(false, b->IsValidBase());
    ASSERT_STREQ(b->path, a.path);
    ASSERT_STREQ(b->script_name, a.script_name);
    ASSERT_STREQ(b->path_info, "/bar");
}
