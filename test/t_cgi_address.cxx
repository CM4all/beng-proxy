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

#include "cgi_address.hxx"
#include "TestPool.hxx"

#include <gtest/gtest.h>

TEST(CgiAddressTest, Uri)
{
    TestPool pool;

    CgiAddress a("/usr/bin/cgi");
    ASSERT_EQ(false, a.IsExpandable());
    ASSERT_STREQ(a.GetURI(pool), "/");

    a.script_name = "/";
    ASSERT_STREQ(a.GetURI(pool), "/");

    a.path_info = "foo";
    ASSERT_STREQ(a.GetURI(pool), "/foo");

    a.query_string = "";
    ASSERT_STREQ(a.GetURI(pool), "/foo?");

    a.query_string = "a=b";
    ASSERT_STREQ(a.GetURI(pool), "/foo?a=b");

    a.path_info = "";
    ASSERT_STREQ(a.GetURI(pool), "/?a=b");

    a.path_info = nullptr;
    ASSERT_STREQ(a.GetURI(pool), "/?a=b");

    a.script_name = "/test.cgi";
    a.path_info = nullptr;
    a.query_string = nullptr;
    ASSERT_STREQ(a.GetURI(pool), "/test.cgi");

    a.path_info = "/foo";
    ASSERT_STREQ(a.GetURI(pool), "/test.cgi/foo");

    a.script_name = "/bar/";
    ASSERT_STREQ(a.GetURI(pool), "/bar/foo");

    a.script_name = "/";
    ASSERT_STREQ(a.GetURI(pool), "/foo");

    a.script_name = nullptr;
    ASSERT_STREQ(a.GetURI(pool), "/foo");
}

TEST(CgiAddressTest, Apply)
{
    TestPool pool;

    CgiAddress a("/usr/bin/cgi");
    a.script_name = "/test.pl";
    a.path_info = "/foo";

    auto b = a.Apply(pool, "");
    ASSERT_EQ((const CgiAddress *)&a, b);

    b = a.Apply(pool, "bar");
    ASSERT_NE(b, nullptr);
    ASSERT_NE(b, &a);
    ASSERT_EQ(false, b->IsValidBase());
    ASSERT_STREQ(b->path, a.path);
    ASSERT_STREQ(b->script_name, a.script_name);
    ASSERT_STREQ(b->path_info, "/bar");

    a.path_info = "/foo/";
    ASSERT_EQ(true, a.IsValidBase());

    b = a.Apply(pool, "bar");
    ASSERT_NE(b, nullptr);
    ASSERT_NE(b, &a);
    ASSERT_EQ(false, b->IsValidBase());
    ASSERT_STREQ(b->path, a.path);
    ASSERT_STREQ(b->script_name, a.script_name);
    ASSERT_STREQ(b->path_info, "/foo/bar");

    b = a.Apply(pool, "/bar");
    ASSERT_NE(b, nullptr);
    ASSERT_NE(b, &a);
    ASSERT_EQ(false, b->IsValidBase());
    ASSERT_STREQ(b->path, a.path);
    ASSERT_STREQ(b->script_name, a.script_name);
    ASSERT_STREQ(b->path_info, "/bar");
}
