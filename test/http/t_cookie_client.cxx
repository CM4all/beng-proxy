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

/**
 * RFC 6265 5.3: a cookie is identified by the tuple (name, domain,
 * path), and setting one replaces only the cookie with that very
 * tuple.  A cookie for a broader path must not delete the cookies of
 * narrower paths.
 */
TEST(CookieClientTest, ReplaceOnlyExactPath)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);
	StringMap headers;

	CookieJar jar;

	cookie_jar_set_cookie2(jar, "a=1;path=\"/foo\"", "foo.bar", "/foo/x");
	cookie_jar_http_header(jar, "foo.bar", "/foo/x", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=1");

	/* this cookie has the same name and domain, but a different
	   path, so it is a different cookie */
	headers.Clear();
	cookie_jar_set_cookie2(jar, "a=2;path=\"/\"", "foo.bar", "/");
	cookie_jar_http_header(jar, "foo.bar", "/foo/x", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=2; a=1");

	/* ... and only the new one applies outside "/foo" */
	headers.Clear();
	cookie_jar_http_header(jar, "foo.bar", "/", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=2");

	/* setting it again replaces just this one */
	headers.Clear();
	cookie_jar_set_cookie2(jar, "a=3;path=\"/\"", "foo.bar", "/");
	cookie_jar_http_header(jar, "foo.bar", "/foo/x", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=3; a=1");
}

/**
 * Like ReplaceOnlyExactPath, but for the domain: a host-only cookie
 * must not delete the cookie which a parent domain has set.
 */
TEST(CookieClientTest, ReplaceOnlyExactDomain)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);
	StringMap headers;

	CookieJar jar;

	cookie_jar_set_cookie2(jar, "a=1;domain=.example.com",
			       "foo.example.com", nullptr);

	/* same name, but this one has no "domain" attribute, so it
	   belongs to "foo.example.com" only */
	cookie_jar_set_cookie2(jar, "a=2", "foo.example.com", nullptr);

	cookie_jar_http_header(jar, "foo.example.com", "/", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=2; a=1");

	/* the domain cookie is still there for the other host */
	headers.Clear();
	cookie_jar_http_header(jar, "bar.example.com", "/", headers, alloc);
	EXPECT_STREQ(headers.Get("cookie"), "a=1");
}
