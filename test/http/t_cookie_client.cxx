// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

/**
 * A server must not be able to set a cookie for a public suffix such
 * as ".com"; that cookie would be sent to every other server.
 */
TEST(CookieClientTest, PublicSuffixDomain)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);
	StringMap headers;

	CookieJar jar;

	/* ".com" is a public suffix and must be rejected ... */
	cookie_jar_set_cookie2(jar, "a=b;domain=.com", "foo.com", nullptr);
	cookie_jar_http_header(jar, "foo.com", "/", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);

	/* ... and must therefore not leak to another server */
	headers.Clear();
	cookie_jar_http_header(jar, "other.com", "/", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);

	/* the same without the leading dot */
	headers.Clear();
	cookie_jar_set_cookie2(jar, "c=d;domain=com", "foo.com", nullptr);
	cookie_jar_http_header(jar, "foo.com", "/", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);
}

/**
 * A fragment of an IP address is not a valid cookie domain either.
 */
TEST(CookieClientTest, IpAddressDomain)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);
	StringMap headers;

	CookieJar jar;

	cookie_jar_set_cookie2(jar, "a=b;domain=.2.3", "10.1.2.3", nullptr);
	cookie_jar_http_header(jar, "10.1.2.3", "/", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);
}

/**
 * A regular domain is still accepted, and is shared between hosts
 * below it.
 */
TEST(CookieClientTest, RegularDomain)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);
	StringMap headers;

	CookieJar jar;

	cookie_jar_set_cookie2(jar, "a=b;domain=.example.com",
			       "foo.example.com", nullptr);
	cookie_jar_http_header(jar, "foo.example.com", "/", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=b");

	/* this is the point of the "domain" attribute: the cookie is
	   sent to other hosts below "example.com" */
	headers.Clear();
	cookie_jar_http_header(jar, "bar.example.com", "/", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=b");

	/* ... but not to an unrelated host */
	headers.Clear();
	cookie_jar_http_header(jar, "example.org", "/", headers, alloc);
	EXPECT_EQ(headers.Get("cookie"), nullptr);
}
