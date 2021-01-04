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

#include "http/CookieServer.hxx"
#include "http/HeaderWriter.hxx"
#include "strmap.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

TEST(CookieServerTest, Basic)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);

	const auto cookies = cookie_map_parse(alloc, "a=b");
	ASSERT_STREQ(cookies.Get("a"), "b");
}

TEST(CookieServerTest, Basic2)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);

	const auto cookies = cookie_map_parse(alloc, "c=d;e=f");
	ASSERT_STREQ(cookies.Get("c"), "d");
	ASSERT_STREQ(cookies.Get("e"), "f");
}

TEST(CookieServerTest, Quoted)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);

	const auto cookies = cookie_map_parse(alloc, "quoted=\"quoted!\\\\");
	ASSERT_STREQ(cookies.Get("quoted"), "quoted!\\");
}

TEST(CookieServerTest, Invalid1)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);

	const auto cookies = cookie_map_parse(alloc, "invalid1=foo\t");
	ASSERT_STREQ(cookies.Get("invalid1"), "foo");
}

TEST(CookieServerTest, Invalid2)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);

	/* this is actually invalid, but unfortunately RFC ignorance is
	   viral, and forces us to accept square brackets :-( */
	const auto cookies = cookie_map_parse(alloc, "invalid2=foo |[bar] ,");
	ASSERT_STREQ(cookies.Get("invalid2"), "foo |[bar] ,");
}

TEST(CookieServerTest, Exclude)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);

	ASSERT_STREQ(cookie_exclude("foo=\"bar\"", "abc", alloc), "foo=\"bar\"");

	ASSERT_EQ(cookie_exclude("foo=\"bar\"", "foo", alloc), nullptr);

	ASSERT_STREQ(cookie_exclude("a=\"b\"", "foo", alloc), "a=\"b\"");

	ASSERT_STREQ(cookie_exclude("a=b", "foo", alloc), "a=b");

	ASSERT_STREQ(cookie_exclude("a=\"b\"; foo=\"bar\"; c=\"d\"", "foo", alloc),
		     "a=\"b\"; c=\"d\"");

	ASSERT_STREQ(cookie_exclude("foo=\"bar\"; c=\"d\"", "foo", alloc),
		     "c=\"d\"");

	ASSERT_STREQ(cookie_exclude("a=\"b\"; foo=\"bar\"", "foo", alloc),
		     "a=\"b\"; ");

	ASSERT_STREQ(cookie_exclude("foo=\"duplicate\"; a=\"b\"; foo=\"bar\"; c=\"d\"",
				    "foo", alloc),
		     "a=\"b\"; c=\"d\"");
}
