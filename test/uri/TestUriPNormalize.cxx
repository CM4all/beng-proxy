// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "uri/PNormalize.hxx"
#include "../TestPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

TEST(UriPNormalize, NormalizeUriPath)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	EXPECT_STREQ(NormalizeUriPath(alloc, "//"), "/");
	EXPECT_STREQ(NormalizeUriPath(alloc, "//."), "/");
	EXPECT_STREQ(NormalizeUriPath(alloc, "."), "");
	EXPECT_STREQ(NormalizeUriPath(alloc, "./"), "");
	EXPECT_STREQ(NormalizeUriPath(alloc, "./."), "");
	EXPECT_STREQ(NormalizeUriPath(alloc, "././"), "");
	EXPECT_STREQ(NormalizeUriPath(alloc, "././././"), "");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/foo/bar"), "/foo/bar");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/foo/./bar"), "/foo/bar");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/./foo/bar"), "/foo/bar");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/foo/bar/./"), "/foo/bar/");
	EXPECT_STREQ(NormalizeUriPath(alloc, "./foo/bar/"), "foo/bar/");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/foo//bar/"), "/foo/bar/");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/foo///bar/"), "/foo/bar/");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/1/2/../3/"), "/1/2/../3/");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/1/2/../../3/"), "/1/2/../../3/");
	EXPECT_STREQ(NormalizeUriPath(alloc, "foo/../bar"), "foo/../bar");
	EXPECT_STREQ(NormalizeUriPath(alloc, "foo//../bar"), "foo/../bar");
	EXPECT_STREQ(NormalizeUriPath(alloc, "foo/.."), "foo/..");
	EXPECT_STREQ(NormalizeUriPath(alloc, "foo/../."), "foo/../");

	EXPECT_STREQ(NormalizeUriPath(alloc, "/../"), "/../");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/.."), "/..");
	EXPECT_STREQ(NormalizeUriPath(alloc, ".."), "..");
	EXPECT_STREQ(NormalizeUriPath(alloc, "/1/2/.."), "/1/2/..");
}
