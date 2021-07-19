/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
