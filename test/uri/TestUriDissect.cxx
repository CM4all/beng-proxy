// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "uri/Dissect.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

TEST(DissectedUriTest, basic)
{
	DissectedUri uri;
	ASSERT_TRUE(uri.Parse("/"));

	ASSERT_EQ(uri.base, "/"sv);
	ASSERT_EQ(uri.args.data(), nullptr);
	ASSERT_EQ(uri.path_info.data(), nullptr);
	ASSERT_EQ(uri.query.data(), nullptr);
}

TEST(DissectedUriTest, Query)
{
	DissectedUri uri;
	ASSERT_TRUE(uri.Parse("/foo?a=b"));

	ASSERT_EQ(uri.base, "/foo"sv);
	ASSERT_EQ(uri.args.data(), nullptr);
	ASSERT_EQ(uri.path_info.data(), nullptr);
	ASSERT_EQ(uri.query, "a=b"sv);
}

TEST(DissectedUriTest, Args)
{
	DissectedUri uri;
	ASSERT_TRUE(uri.Parse("/foo;c=d?a=b"));

	ASSERT_EQ(uri.base, "/foo"sv);
	ASSERT_EQ(uri.args, "c=d"sv);
	ASSERT_EQ(uri.path_info.data(), nullptr);
	ASSERT_EQ(uri.query, "a=b"sv);
}

TEST(DissectedUriTest, ArgsPath)
{
	DissectedUri uri;
	ASSERT_TRUE(uri.Parse("/foo;c=d/bar?a=b"));

	ASSERT_EQ(uri.base, "/foo"sv);
	ASSERT_EQ(uri.args, "c=d"sv);
	ASSERT_EQ(uri.path_info, "/bar"sv);
	ASSERT_EQ(uri.query, "a=b"sv);
}
