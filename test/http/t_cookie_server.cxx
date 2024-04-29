// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/CookieServer.hxx"
#include "http/HeaderWriter.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

TEST(CookieServerTest, Exclude)
{
	RootPool pool;
	const AllocatorPtr alloc(pool);

	ASSERT_STREQ(cookie_exclude("foo=\"bar\"", "abc", alloc), "foo=\"bar\"");

	ASSERT_EQ(cookie_exclude("foo=\"bar\"", "foo", alloc), nullptr);

	ASSERT_STREQ(cookie_exclude("a=\"b\"", "foo", alloc), "a=\"b\"");

	ASSERT_STREQ(cookie_exclude("a=b", "foo", alloc), "a=b");
	ASSERT_STREQ(cookie_exclude("foo=bar;a=b", "foo", alloc), "a=b");
	ASSERT_STREQ(cookie_exclude("a=b;foo=bar", "foo", alloc), "a=b");

	ASSERT_STREQ(cookie_exclude("a=\"b\"; foo=\"bar\"; c=\"d\"", "foo", alloc),
		     "a=\"b\"; c=\"d\"");

	ASSERT_STREQ(cookie_exclude("foo=\"bar\"; c=\"d\"", "foo", alloc),
		     "c=\"d\"");

	ASSERT_STREQ(cookie_exclude("a=\"b\"; foo=\"bar\"", "foo", alloc),
		     "a=\"b\"; ");

	ASSERT_STREQ(cookie_exclude("foo=\"duplicate\"; a=\"b\"; foo=\"bar\"; c=\"d\"",
				    "foo", alloc),
		     "a=\"b\";c=\"d\"");
}
