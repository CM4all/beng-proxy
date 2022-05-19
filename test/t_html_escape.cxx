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

#include "escape/HTML.hxx"
#include "escape/Class.hxx"
#include "escape/Static.hxx"

#include <gtest/gtest.h>

#include <string_view>

using std::string_view_literals::operator""sv;

static const char *
html_unescape(const char *p)
{
	return unescape_static(&html_escape_class, p);
}

static std::string_view
html_unescape_inplace(char *p, size_t length)
{
	return {p, unescape_inplace(&html_escape_class, p, length)};
}

TEST(HtmlEscape, Basic)
{
	ASSERT_STREQ(html_unescape("foo bar"), "foo bar");
	ASSERT_STREQ(html_unescape("foo&amp;bar"), "foo&bar");
	ASSERT_STREQ(html_unescape("&lt;&gt;"), "<>");
	ASSERT_STREQ(html_unescape("&quot;"), "\"");
	ASSERT_STREQ(html_unescape("&amp;amp;"), "&amp;");
	ASSERT_STREQ(html_unescape("&amp;&&quot;"), "&&\"");
	ASSERT_STREQ(html_unescape("&gt&lt;&apos;"), "&gt<'");
	ASSERT_STREQ(html_unescape("&#10;"), "\n");
	ASSERT_STREQ(html_unescape("&#xa;"), "\n");
	ASSERT_STREQ(html_unescape("&#xfc;"), "\xc3\xbc");
	ASSERT_STREQ(html_unescape("&#x10ffff;"), "\xf4\x8f\xbf\xbf");

	char a[] = "foo bar";
	ASSERT_EQ(html_unescape_inplace(a, sizeof(a) - 1), "foo bar"sv);

	char e[] = "foo&amp;bar";
	ASSERT_EQ(html_unescape_inplace(e, sizeof(e) - 1), "foo&bar"sv);

	char f[] = "&lt;foo&gt;bar&apos;";
	ASSERT_EQ(html_unescape_inplace(f, sizeof(f) - 1), "<foo>bar'"sv);

	char b[] = "&lt;&gt;&apos;";
	ASSERT_EQ(html_unescape_inplace(b, sizeof(b) - 1), "<>'"sv);

	char c[] = "&quot";
	ASSERT_EQ(html_unescape_inplace(c, sizeof(c) - 1), "&quot"sv);

	char d[] = "&amp;&&quot;";
	ASSERT_EQ(html_unescape_inplace(d, sizeof(d) - 1), "&&\""sv);
}
