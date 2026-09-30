// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "uri/PNormalize.hxx"
#include "../TestPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

TEST(UriPNormalize, NormalizeUriPath)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	EXPECT_EQ(NormalizeUriPath(alloc, "//"sv), "/"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "//."sv), "/"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "."sv), ""sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "./"sv), ""sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "./."sv), ""sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "././"sv), ""sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "././././"sv), ""sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/foo/bar"sv), "/foo/bar"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/foo/./bar"sv), "/foo/bar"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/./foo/bar"sv), "/foo/bar"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/foo/bar/./"sv), "/foo/bar/"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "./foo/bar/"sv), "foo/bar/"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/foo//bar/"sv), "/foo/bar/"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/foo///bar/"sv), "/foo/bar/"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/1/2/../3/"sv), "/1/2/../3/"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/1/2/../../3/"sv), "/1/2/../../3/"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "foo/../bar"sv), "foo/../bar"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "foo//../bar"sv), "foo/../bar"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "foo/.."sv), "foo/.."sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "foo/../."sv), "foo/../"sv);

	EXPECT_EQ(NormalizeUriPath(alloc, "/../"sv), "/../"sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/.."sv), "/.."sv);
	EXPECT_EQ(NormalizeUriPath(alloc, ".."sv), ".."sv);
	EXPECT_EQ(NormalizeUriPath(alloc, "/1/2/.."sv), "/1/2/.."sv);
}
