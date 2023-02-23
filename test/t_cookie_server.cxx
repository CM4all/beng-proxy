// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
