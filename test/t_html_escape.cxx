/*
 * Copyright 2007-2017 Content Management AG
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

#include "escape_html.hxx"
#include "escape_class.hxx"
#include "escape_static.hxx"

#include <gtest/gtest.h>

static const char *
html_unescape(const char *p)
{
    return unescape_static(&html_escape_class, p);
}

static size_t
html_unescape_inplace(char *p, size_t length)
{
    return unescape_inplace(&html_escape_class, p, length);
}

TEST(HtmlEscape, Basic)
{
    size_t length;

    ASSERT_STREQ(html_unescape("foo bar"), "foo bar");
    ASSERT_STREQ(html_unescape("foo&amp;bar"), "foo&bar");
    ASSERT_STREQ(html_unescape("&lt;&gt;"), "<>");
    ASSERT_STREQ(html_unescape("&quot;"), "\"");
    ASSERT_STREQ(html_unescape("&amp;amp;"), "&amp;");
    ASSERT_STREQ(html_unescape("&amp;&&quot;"), "&&\"");
    ASSERT_STREQ(html_unescape("&gt&lt;&apos;"), "&gt<'");

    char a[] = "foo bar";
    length = html_unescape_inplace(a, sizeof(a) - 1);
    ASSERT_EQ(length, 7);

    char e[] = "foo&amp;bar";
    length = html_unescape_inplace(e, sizeof(e) - 1);
    ASSERT_EQ(length, 7);
    ASSERT_EQ(memcmp(e, "foo&bar", 7), 0);

    char f[] = "&lt;foo&gt;bar&apos;";
    length = html_unescape_inplace(f, sizeof(f) - 1);
    ASSERT_EQ(length, 9);
    ASSERT_EQ(memcmp(f, "<foo>bar'", 9), 0);

    char b[] = "&lt;&gt;&apos;";
    length = html_unescape_inplace(b, sizeof(b) - 1);
    ASSERT_EQ(length, 3);
    ASSERT_EQ(memcmp(b, "<>'", 3), 0);

    char c[] = "&quot";
    length = html_unescape_inplace(c, sizeof(c) - 1);
    ASSERT_EQ(length, 5);
    ASSERT_EQ(memcmp(c, "&quot", 5), 0);

    char d[] = "&amp;&&quot;";
    length = html_unescape_inplace(d, sizeof(d) - 1);
    ASSERT_EQ(length, 3);
    ASSERT_EQ(memcmp(d, "&&\"", 3), 0);
}
