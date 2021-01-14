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

#include "http/CookieClient.hxx"
#include "http/CookieJar.hxx"
#include "http/HeaderWriter.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <gtest/gtest.h>

#include <unistd.h>

TEST(CookieClientTest, Test1)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);
	StringMap headers;

	CookieJar jar;

	/* empty cookie jar */
	cookie_jar_http_header(jar, "foo.bar", "/", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);
	EXPECT_EQ(headers.Get("cookie2"), nullptr);

	/* wrong domain */
	cookie_jar_set_cookie2(jar, "a=b", "other.domain", nullptr);
	cookie_jar_http_header(jar, "foo.bar", "/", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);
	EXPECT_EQ(headers.Get("cookie2"), nullptr);

	/* correct domain */
	cookie_jar_set_cookie2(jar, "a=b", "foo.bar", nullptr);
	cookie_jar_http_header(jar, "foo.bar", "/", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=b");

	/* another cookie */
	headers.Clear();
	cookie_jar_set_cookie2(jar, "c=d", "foo.bar", nullptr);
	cookie_jar_http_header(jar, "foo.bar", "/", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "c=d; a=b");

	/* delete a cookie */
	headers.Clear();
	cookie_jar_set_cookie2(jar, "c=xyz;max-age=0", "foo.bar", nullptr);
	cookie_jar_http_header(jar, "foo.bar", "/", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=b");

	/* other domain */
	headers.Clear();
	cookie_jar_http_header(jar, "other.domain", "/some_path", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=b");
}

TEST(CookieClientTest, Test2)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);
	StringMap headers;

	/* wrong path */
	CookieJar jar;

	cookie_jar_set_cookie2(jar, "a=b;path=\"/foo\"", "foo.bar", "/bar/x");
	cookie_jar_http_header(jar, "foo.bar", "/", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);
	EXPECT_EQ(headers.Get("cookie2"), nullptr);

	/* correct path */
	headers.Clear();
	cookie_jar_set_cookie2(jar, "a=b;path=\"/bar\"", "foo.bar", "/bar/x");
	cookie_jar_http_header(jar, "foo.bar", "/bar", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=b");

	/* delete: path mismatch */
	headers.Clear();
	cookie_jar_set_cookie2(jar, "a=b;path=\"/foo\";max-age=0",
			       "foo.bar", "/foo/x");
	cookie_jar_http_header(jar, "foo.bar", "/bar", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=b");

	/* delete: path match */
	headers.Clear();
	cookie_jar_set_cookie2(jar, "a=b;path=\"/bar\";max-age=0",
			       "foo.bar", "/bar/x");
	cookie_jar_http_header(jar, "foo.bar", "/bar", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);
	EXPECT_EQ(headers.Get("cookie2"), nullptr);
}
