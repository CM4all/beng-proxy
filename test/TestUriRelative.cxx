/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "uri/Relative.hxx"
#include "util/StringView.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

TEST(UriRelativeTest, Relative)
{
	EXPECT_EQ(uri_relative(""sv, "/foo/"sv).data, nullptr);
	EXPECT_EQ(uri_relative("/foo/"sv, ""sv).data, nullptr);
	EXPECT_EQ(uri_relative("/foo/"sv, "/foo"sv).data, nullptr);
	EXPECT_NE(uri_relative("/foo/"sv, "/foo/"sv).data, nullptr);
	EXPECT_EQ(uri_relative("/foo/"sv, "/foo/"sv), ""sv);
	EXPECT_EQ(uri_relative("/foo/"sv, "/foo/bar"sv), "bar"sv);
	EXPECT_EQ(uri_relative("/"sv, "/foo/bar"sv), "foo/bar"sv);
	EXPECT_EQ(uri_relative("http://host.name/foo/"sv, "http://host.name/foo"sv).data, nullptr);
	EXPECT_NE(uri_relative("http://host.name/"sv, "http://host.name"sv).data, nullptr);
	EXPECT_EQ(uri_relative("http://host.name/"sv, "http://host.name"sv), ""sv);
}
