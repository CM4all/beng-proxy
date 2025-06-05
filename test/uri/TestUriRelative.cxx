// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "uri/Relative.hxx"

#include <string_view>

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

TEST(UriRelativeTest, Relative)
{
	EXPECT_EQ(uri_relative(""sv, "/foo/"sv).data(), nullptr);
	EXPECT_EQ(uri_relative("/foo/"sv, ""sv).data(), nullptr);
	EXPECT_EQ(uri_relative("/foo/"sv, "/foo"sv).data(), nullptr);
	EXPECT_NE(uri_relative("/foo/"sv, "/foo/"sv).data(), nullptr);
	EXPECT_EQ(uri_relative("/foo/"sv, "/foo/"sv), ""sv);
	EXPECT_EQ(uri_relative("/foo/"sv, "/foo/bar"sv), "bar"sv);
	EXPECT_EQ(uri_relative("/"sv, "/foo/bar"sv), "foo/bar"sv);
	EXPECT_EQ(uri_relative("http://host.name/foo/"sv, "http://host.name/foo"sv).data(), nullptr);
	EXPECT_NE(uri_relative("http://host.name/"sv, "http://host.name"sv).data(), nullptr);
	EXPECT_EQ(uri_relative("http://host.name/"sv, "http://host.name"sv), ""sv);
}
