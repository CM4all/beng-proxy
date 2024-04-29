// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/CookieExtract.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

TEST(CookieExtract, Basic)
{
	constexpr auto input = "a=b"sv;
	ASSERT_EQ(ExtractCookieRaw(input, "c").data(), nullptr);
	ASSERT_EQ(ExtractCookieRaw(input, "a"), "b"sv);
}

TEST(CookieExtract, Basic2)
{
	constexpr auto input = "c=d;e=f"sv;
	ASSERT_EQ(ExtractCookieRaw(input, "c"), "d"sv);
	ASSERT_EQ(ExtractCookieRaw(input, "e"), "f"sv);
}

TEST(CookieExtract, Quoted)
{
	constexpr auto input = "quoted=\"quoted!\\\\"sv;
	ASSERT_EQ(ExtractCookieRaw(input, "quoted"), "quoted!\\\\");
}

TEST(CookieExtract, Quoted2)
{
	constexpr auto input = "quoted=\"quoted!\\\\\""sv;
	ASSERT_EQ(ExtractCookieRaw(input, "quoted"), "quoted!\\\\");
}

TEST(CookieExtract, Invalid1)
{
	constexpr auto input = "invalid1=foo\t"sv;
	ASSERT_EQ(ExtractCookieRaw(input, "invalid1"), "foo");
}

TEST(CookieExtract, Invalid2)
{
	/* this is actually invalid, but unfortunately RFC ignorance is
	   viral, and forces us to accept square brackets :-( */
	constexpr auto input = "invalid2=foo |[bar] ,"sv;
	ASSERT_EQ(ExtractCookieRaw(input, "invalid2"), "foo |[bar] ,");
}

/**
 * Other cookies are RFC-ignorant.
 */
TEST(CookieExtract, Invalid3)
{
	ASSERT_EQ(ExtractCookieRaw("xyz=[{(,)}];foo=bar;abc=(,)", "foo"), "bar");
}
