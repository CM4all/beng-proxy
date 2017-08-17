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

#include "uri/uri_escape.hxx"
#include "util/StringView.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>

static constexpr struct UriEscapeData {
    const char *escaped, *unescaped;
} uri_escape_data[] = {
    { "", "" },
    { "%20", " " },
    { "%ff", "\xff" },
    { "%00", nullptr },
    { "%", nullptr },
    { "%1", nullptr },
    { "%gg", nullptr },
    { "foo", "foo" },
    { "foo%20bar", "foo bar" },
    { "foo%25bar", "foo%bar" },
    { "foo%2525bar", "foo%25bar" },
};

TEST(UriEscapeTest, Escape)
{
    for (auto i : uri_escape_data) {
        if (i.unescaped == nullptr)
            continue;

        char buffer[256];
        size_t length = uri_escape(buffer, i.unescaped);
        ASSERT_EQ(length, strlen(i.escaped));
        ASSERT_EQ(memcmp(buffer, i.escaped, length), 0);
    }
}

TEST(UriEscapeTest, Unescape)
{
    for (auto i : uri_escape_data) {
        char buffer[256];
        strcpy(buffer, i.escaped);

        auto result = uri_unescape(buffer, i.escaped);
        if (i.unescaped == nullptr) {
            ASSERT_EQ(result, (char *)nullptr);
        } else {
            size_t length = result - buffer;
            ASSERT_EQ(length, strlen(i.unescaped));
            ASSERT_EQ(memcmp(buffer, i.unescaped, length), 0);
        }
    }
}
