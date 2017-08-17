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

#include "regex.hxx"
#include "pexpand.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>

TEST(RegexTest, Match1)
{
    UniqueRegex r;
    ASSERT_FALSE(r.IsDefined());
    r.Compile(".", false, false);
    ASSERT_TRUE(r.IsDefined());
    ASSERT_TRUE(r.Match("a"));
    ASSERT_TRUE(r.Match("abc"));
}

TEST(RegexTest, Match2)
{
    UniqueRegex r = UniqueRegex();
    ASSERT_FALSE(r.IsDefined());
    r.Compile("..", false, false);
    ASSERT_TRUE(r.IsDefined());
    ASSERT_FALSE(r.Match("a"));
    ASSERT_TRUE(r.Match("abc"));
}

TEST(RegexTest, NotAnchored)
{
    UniqueRegex r = UniqueRegex();
    ASSERT_FALSE(r.IsDefined());
    r.Compile("/foo/", false, false);
    ASSERT_TRUE(r.IsDefined());
    ASSERT_TRUE(r.Match("/foo/"));
    ASSERT_TRUE(r.Match("/foo/bar"));
    ASSERT_TRUE(r.Match("foo/foo/"));
}

TEST(RegexTest, Anchored)
{
    UniqueRegex r = UniqueRegex();
    ASSERT_FALSE(r.IsDefined());
    r.Compile("/foo/", true, false);
    ASSERT_TRUE(r.IsDefined());
    ASSERT_TRUE(r.Match("/foo/"));
    ASSERT_TRUE(r.Match("/foo/bar"));
    ASSERT_FALSE(r.Match("foo/foo/"));
}

TEST(RegexTest, Expand)
{
    UniqueRegex r;
    ASSERT_FALSE(r.IsDefined());
    r.Compile("^/foo/(\\w+)/([^/]+)/(.*)$", false, true);
    ASSERT_TRUE(r.IsDefined());

    ASSERT_FALSE(r.Match("a"));

    auto match_info = r.MatchCapture("/foo/bar/a/b/c.html");
    ASSERT_TRUE(match_info.IsDefined());

    TestPool pool;
    AllocatorPtr alloc(pool);

    auto e = expand_string(alloc, "\\1-\\2-\\3-\\\\", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "bar-a-b/c.html-\\"), 0);

    match_info = r.MatchCapture("/foo/bar/a/b/");
    ASSERT_TRUE(match_info.IsDefined());

    e = expand_string(alloc, "\\1-\\2-\\3-\\\\", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "bar-a-b/-\\"), 0);

    match_info = r.MatchCapture("/foo/bar/a%20b/c%2520.html");
    ASSERT_TRUE(match_info.IsDefined());

    e = expand_string_unescaped(alloc, "\\1-\\2-\\3", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "bar-a b-c%20.html"), 0);

    ASSERT_THROW(expand_string_unescaped(alloc, "\\4", match_info),
                 std::runtime_error);
}

TEST(RegexTest, ExpandMalformedUriEscape)
{
    UniqueRegex r;
    ASSERT_FALSE(r.IsDefined());
    r.Compile("^(.*)$", false, true);
    ASSERT_TRUE(r.IsDefined());

    auto match_info = r.MatchCapture("%xxx");
    ASSERT_TRUE(match_info.IsDefined());

    TestPool pool;
    AllocatorPtr alloc(pool);

    auto e = expand_string(alloc, "-\\1-", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "-%xxx-"), 0);

    ASSERT_THROW(expand_string_unescaped(alloc, "-\\1-", match_info),
                 std::runtime_error);
}

TEST(RegexTest, ExpandOptional)
{
    UniqueRegex r;
    ASSERT_FALSE(r.IsDefined());
    r.Compile("^(a)(b)?(c)$", true, true);
    ASSERT_TRUE(r.IsDefined());

    auto match_info = r.MatchCapture("abc");
    ASSERT_TRUE(match_info.IsDefined());

    TestPool pool;
    AllocatorPtr alloc(pool);

    auto e = expand_string(alloc, "\\1-\\2-\\3", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "a-b-c"), 0);

    match_info = r.MatchCapture("ac");
    ASSERT_TRUE(match_info.IsDefined());
    e = expand_string(alloc, "\\1-\\2-\\3", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "a--c"), 0);
}

TEST(RegexTest, ExpandOptionalLast)
{
    UniqueRegex r;
    ASSERT_FALSE(r.IsDefined());
    r.Compile("^(a)(b)?(c)?$", true, true);
    ASSERT_TRUE(r.IsDefined());

    auto match_info = r.MatchCapture("abc");
    ASSERT_TRUE(match_info.IsDefined());

    TestPool pool;
    AllocatorPtr alloc(pool);

    auto e = expand_string(alloc, "\\1-\\2-\\3", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "a-b-c"), 0);

    match_info = r.MatchCapture("ac");
    ASSERT_TRUE(match_info.IsDefined());
    e = expand_string(alloc, "\\1-\\2-\\3", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "a--c"), 0);

    match_info = r.MatchCapture("ab");
    ASSERT_TRUE(match_info.IsDefined());
    e = expand_string(alloc, "\\1-\\2-\\3", match_info);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(strcmp(e, "a-b-"), 0);
}
