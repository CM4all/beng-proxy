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

#include "escape_css.hxx"
#include "escape_class.hxx"

#include <gtest/gtest.h>

#include <string.h>

static void
check_unescape_find(const char *p, size_t offset)
{
    ASSERT_EQ(unescape_find(&css_escape_class, p), p + offset);
}

static void
check_unescape(const char *p, const char *q)
{
    static char buffer[1024];
    size_t l = unescape_buffer(&css_escape_class, p, buffer);
    ASSERT_EQ(l, strlen(q));
    ASSERT_EQ(memcmp(buffer, q, l), 0);
}

static void
check_escape_find(const char *p, size_t offset)
{
    ASSERT_EQ(escape_find(&css_escape_class, p), p + offset);
}

static void
check_escape(const char *p, const char *q)
{
    static char buffer[1024];
    size_t l = escape_buffer(&css_escape_class, p, buffer);
    ASSERT_EQ(l, strlen(q));
    ASSERT_EQ(memcmp(buffer, q, l), 0);
}

TEST(CssEscape, Basic)
{
    assert(unescape_find(&css_escape_class, "foobar123") == NULL);
    check_unescape_find("\\", 0);
    check_unescape_find("foo\\\\", 3);
    check_unescape("foo\\\\", "foo\\");

    check_escape_find("foo'bar", 3);
    check_escape_find("foo\\bar", 3);
    check_escape_find("foo\"bar", 3);

    ASSERT_STREQ(escape_char(&css_escape_class, '\''), "\\'");
    ASSERT_STREQ(escape_char(&css_escape_class, '\\'), "\\\\");

    check_escape("foobar", "foobar");
    check_escape("foo\\bar", "foo\\\\bar");
    check_escape("foo'bar", "foo\\'bar");
}
