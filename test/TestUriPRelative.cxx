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

	ASSERT_STREQ(uri_compress(alloc, "/foo/bar"), "/foo/bar");
	ASSERT_STREQ(uri_compress(alloc, "/foo/./bar"), "/foo/bar");
	ASSERT_STREQ(uri_compress(alloc, "/./foo/bar"), "/foo/bar");
	ASSERT_STREQ(uri_compress(alloc, "/foo/bar/./"), "/foo/bar/");
	ASSERT_STREQ(uri_compress(alloc, "./foo/bar/"), "foo/bar/");
	ASSERT_STREQ(uri_compress(alloc, "/foo//bar/"), "/foo/bar/");
	ASSERT_STREQ(uri_compress(alloc, "/foo///bar/"), "/foo/bar/");
	ASSERT_STREQ(uri_compress(alloc, "/1/2/../3/"), "/1/3/");
	ASSERT_STREQ(uri_compress(alloc, "/1/2/../../3/"), "/3/");
	ASSERT_STREQ(uri_compress(alloc, "foo/../bar"), "bar");
	ASSERT_STREQ(uri_compress(alloc, "foo//../bar"), "bar");
	ASSERT_STREQ(uri_compress(alloc, "foo/.."), "");
	ASSERT_STREQ(uri_compress(alloc, "foo/../."), "");

	ASSERT_EQ(uri_compress(alloc, "/1/2/../../../3/"), nullptr);
	ASSERT_EQ(uri_compress(alloc, "/../"), nullptr);
	ASSERT_EQ(uri_compress(alloc, "/a/../../"), nullptr);
	ASSERT_EQ(uri_compress(alloc, "/.."), nullptr);
	ASSERT_EQ(uri_compress(alloc, ".."), nullptr);
	ASSERT_STREQ(uri_compress(alloc, "/1/2/.."), "/1/");
}

TEST(UriRelativeTest, Absolute)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/", "foo"), "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar", "foo"), "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar/", "foo"), "http://localhost/bar/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar/", "/foo"), "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar/",
				  "http://localhost/bar/foo"),
		     "http://localhost/bar/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar/",
				  "http://localhost/foo"),
		     "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost", "foo"),
		     "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/", "foo"), "/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/bar", "foo"), "/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/bar/", "foo"), "/bar/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/bar/", "/foo"), "/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/bar", "?foo"), "/bar?foo");

	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/foo/",
				  "//example.com/bar"),
		     "http://example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "ftp://localhost/foo/",
				  "//example.com/bar"),
		     "ftp://example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "/foo/", "//example.com/bar"),
		     "//example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "//example.com/foo/", "bar"),
		     "//example.com/foo/bar");

	ASSERT_STREQ(uri_absolute(alloc, "//example.com/foo/", "/bar"),
		     "//example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "//example.com", "bar"),
		     "//example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "//example.com", "/bar"),
		     "//example.com/bar");
}
