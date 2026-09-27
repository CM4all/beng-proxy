// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "uri/PRelative.hxx"
#include "../TestPool.hxx"
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
	EXPECT_EQ(uri_compress(alloc, "foo/../../bar"), nullptr);
	EXPECT_EQ(uri_compress(alloc, "foo/../.."), nullptr);
	EXPECT_EQ(uri_compress(alloc, "a/b/../../../c"), nullptr);
	EXPECT_STREQ(uri_compress(alloc, "/1/2/.."), "/1/");
}

/**
 * uri_compress() resolves dot segments textually, so a percent-encoded
 * dot would be invisible to it, but would be resolved by the server on
 * the other end.
 */
TEST(UriRelativeTest, CompressEncodedDot)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	EXPECT_EQ(uri_compress(alloc, "%2e%2e/x"), nullptr);
	EXPECT_EQ(uri_compress(alloc, "%2E%2E/x"), nullptr);
	EXPECT_EQ(uri_compress(alloc, "/foo/%2e./x"), nullptr);
	EXPECT_EQ(uri_compress(alloc, "/foo/.%2E/x"), nullptr);
	EXPECT_EQ(uri_compress(alloc, "/foo/%2e%2E/x"), nullptr);

	/* a single encoded dot is refused as well, because it can
	   combine with a literal one */
	EXPECT_EQ(uri_compress(alloc, "/foo/%2e"), nullptr);

	/* other percent-encodings are still accepted */
	EXPECT_STREQ(uri_compress(alloc, "/foo/%20bar"), "/foo/%20bar");
	EXPECT_STREQ(uri_compress(alloc, "/foo%2fbar"), "/foo%2fbar");
	EXPECT_STREQ(uri_compress(alloc, "/foo/%zz"), "/foo/%zz");
	EXPECT_STREQ(uri_compress(alloc, "/foo/%2fbar/../x"), "/foo/x");

	/* a truncated escape at the end of the string must not be
	   read past */
	EXPECT_STREQ(uri_compress(alloc, "/foo/%"), "/foo/%");
	EXPECT_STREQ(uri_compress(alloc, "/foo/%2"), "/foo/%2");

	/* the scan must not skip an escape which follows another
	   percent character */
	EXPECT_EQ(uri_compress(alloc, "/foo/%%2e%2e/x"), nullptr);
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
