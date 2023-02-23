// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
