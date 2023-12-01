// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "uri/PRelative.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

TEST(UriRelativeTest, Compress)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	EXPECT_STREQ(uri_compress(alloc, "/foo/bar"), "/foo/bar");
	EXPECT_STREQ(uri_compress(alloc, "/foo/./bar"), "/foo/bar");
	EXPECT_STREQ(uri_compress(alloc, "/./foo/bar"), "/foo/bar");
	EXPECT_STREQ(uri_compress(alloc, "/foo/bar/./"), "/foo/bar/");
	EXPECT_STREQ(uri_compress(alloc, "./foo/bar/"), "foo/bar/");
	EXPECT_STREQ(uri_compress(alloc, "/foo//bar/"), "/foo/bar/");
	EXPECT_STREQ(uri_compress(alloc, "/foo///bar/"), "/foo/bar/");
	EXPECT_STREQ(uri_compress(alloc, "/1/2/../3/"), "/1/3/");
	EXPECT_STREQ(uri_compress(alloc, "/1/2/../../3/"), "/3/");
	EXPECT_STREQ(uri_compress(alloc, "foo/../bar"), "bar");
	EXPECT_STREQ(uri_compress(alloc, "foo//../bar"), "bar");
	EXPECT_STREQ(uri_compress(alloc, "foo/.."), "");
	EXPECT_STREQ(uri_compress(alloc, "foo/."), "foo/");
	EXPECT_STREQ(uri_compress(alloc, "foo/../."), "");

	EXPECT_EQ(uri_compress(alloc, "/1/2/../../../3/"), nullptr);
	EXPECT_EQ(uri_compress(alloc, "/../"), nullptr);
	EXPECT_EQ(uri_compress(alloc, "/a/../../"), nullptr);
	EXPECT_EQ(uri_compress(alloc, "/.."), nullptr);
	EXPECT_EQ(uri_compress(alloc, ".."), nullptr);
	EXPECT_STREQ(uri_compress(alloc, "/1/2/.."), "/1/");
}

TEST(UriRelativeTest, Absolute)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	EXPECT_STREQ(uri_absolute(alloc, "http://localhost/", "foo"), "http://localhost/foo");
	EXPECT_STREQ(uri_absolute(alloc, "http://localhost/bar", "foo"), "http://localhost/foo");
	EXPECT_STREQ(uri_absolute(alloc, "http://localhost/bar/", "foo"), "http://localhost/bar/foo");
	EXPECT_STREQ(uri_absolute(alloc, "http://localhost/bar/", "/foo"), "http://localhost/foo");
	EXPECT_STREQ(uri_absolute(alloc, "http://localhost/bar/",
				  "http://localhost/bar/foo"),
		     "http://localhost/bar/foo");
	EXPECT_STREQ(uri_absolute(alloc, "http://localhost/bar/",
				  "http://localhost/foo"),
		     "http://localhost/foo");
	EXPECT_STREQ(uri_absolute(alloc, "http://localhost", "foo"),
		     "http://localhost/foo");
	EXPECT_STREQ(uri_absolute(alloc, "/", "foo"), "/foo");
	EXPECT_STREQ(uri_absolute(alloc, "/bar", "foo"), "/foo");
	EXPECT_STREQ(uri_absolute(alloc, "/bar/", "foo"), "/bar/foo");
	EXPECT_STREQ(uri_absolute(alloc, "/bar/", "/foo"), "/foo");
	EXPECT_STREQ(uri_absolute(alloc, "/bar", "?foo"), "/bar?foo");

	EXPECT_STREQ(uri_absolute(alloc, "http://localhost/foo/",
				  "//example.com/bar"),
		     "http://example.com/bar");

	EXPECT_STREQ(uri_absolute(alloc, "ftp://localhost/foo/",
				  "//example.com/bar"),
		     "ftp://example.com/bar");

	EXPECT_STREQ(uri_absolute(alloc, "/foo/", "//example.com/bar"),
		     "//example.com/bar");

	EXPECT_STREQ(uri_absolute(alloc, "//example.com/foo/", "bar"),
		     "//example.com/foo/bar");

	EXPECT_STREQ(uri_absolute(alloc, "//example.com/foo/", "/bar"),
		     "//example.com/bar");

	EXPECT_STREQ(uri_absolute(alloc, "//example.com", "bar"),
		     "//example.com/bar");

	EXPECT_STREQ(uri_absolute(alloc, "//example.com", "/bar"),
		     "//example.com/bar");
}
