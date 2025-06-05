// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "uri/RedirectHttps.hxx"
#include "../TestPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

#include <string.h>

TEST(TestRedirectHttps, Basic)
{
	TestPool pool;
	const AllocatorPtr alloc(pool);

	ASSERT_EQ(MakeHttpsRedirect(alloc, "localhost", 0, "/foo"),
		  "https://localhost/foo");

	ASSERT_EQ(MakeHttpsRedirect(alloc, "localhost:80", 0, "/foo"),
		  "https://localhost/foo");

	ASSERT_EQ(MakeHttpsRedirect(alloc, "localhost:80", 443, "/foo"),
		  "https://localhost/foo");

	ASSERT_EQ(MakeHttpsRedirect(alloc, "localhost:80", 444, "/foo"),
		  "https://localhost:444/foo");
}

TEST(TestRedirectHttps, IPv6)
{
	TestPool pool;
	const AllocatorPtr alloc(pool);

	ASSERT_EQ(MakeHttpsRedirect(alloc, "::", 0, "/foo"),
		  "https://::/foo");

	ASSERT_EQ(MakeHttpsRedirect(alloc, "[::]:80", 0, "/foo"),
		  "https://::/foo");

	ASSERT_EQ(MakeHttpsRedirect(alloc, "::", 444, "/foo"),
		  "https://[::]:444/foo");
}
