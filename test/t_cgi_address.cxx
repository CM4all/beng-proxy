/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "cgi/Address.hxx"
#include "AllocatorPtr.hxx"
#include "TestPool.hxx"

#include <gtest/gtest.h>

TEST(CgiAddressTest, Uri)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	CgiAddress a("/usr/bin/cgi");
	ASSERT_FALSE(a.IsExpandable());
	ASSERT_STREQ(a.GetURI(alloc), "/");

	a.script_name = "/";
	ASSERT_STREQ(a.GetURI(alloc), "/");

	a.path_info = "foo";
	ASSERT_STREQ(a.GetURI(alloc), "/foo");

	a.query_string = "";
	ASSERT_STREQ(a.GetURI(alloc), "/foo?");

	a.query_string = "a=b";
	ASSERT_STREQ(a.GetURI(alloc), "/foo?a=b");

	a.path_info = "";
	ASSERT_STREQ(a.GetURI(alloc), "/?a=b");

	a.path_info = nullptr;
	ASSERT_STREQ(a.GetURI(alloc), "/?a=b");

	a.script_name = "/test.cgi";
	a.path_info = nullptr;
	a.query_string = nullptr;
	ASSERT_STREQ(a.GetURI(alloc), "/test.cgi");

	a.path_info = "/foo";
	ASSERT_STREQ(a.GetURI(alloc), "/test.cgi/foo");

	a.script_name = "/bar/";
	ASSERT_STREQ(a.GetURI(alloc), "/bar/foo");

	a.script_name = "/";
	ASSERT_STREQ(a.GetURI(alloc), "/foo");

	a.script_name = nullptr;
	ASSERT_STREQ(a.GetURI(alloc), "/foo");
}

TEST(CgiAddressTest, Apply)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	CgiAddress a("/usr/bin/cgi");
	a.script_name = "/test.pl";
	a.path_info = "/foo";

	auto b = a.Apply(alloc, "");
	ASSERT_EQ((const CgiAddress *)&a, b);

	b = a.Apply(alloc, "bar");
	ASSERT_NE(b, nullptr);
	ASSERT_NE(b, &a);
	ASSERT_FALSE(b->IsValidBase());
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->script_name, a.script_name);
	ASSERT_STREQ(b->path_info, "/bar");

	a.path_info = "/foo/";
	ASSERT_EQ(true, a.IsValidBase());

	b = a.Apply(alloc, "bar");
	ASSERT_NE(b, nullptr);
	ASSERT_NE(b, &a);
	ASSERT_FALSE(b->IsValidBase());
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->script_name, a.script_name);
	ASSERT_STREQ(b->path_info, "/foo/bar");

	b = a.Apply(alloc, "/bar");
	ASSERT_NE(b, nullptr);
	ASSERT_NE(b, &a);
	ASSERT_FALSE(b->IsValidBase());
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->script_name, a.script_name);
	ASSERT_STREQ(b->path_info, "/bar");
}

gcc_pure
static auto
MakeCgiAddress(const char *executable_path, const char *script_name,
	       const char *path_info) noexcept
{
	CgiAddress address(executable_path);
	address.script_name = script_name;
	address.path_info = path_info;
	return address;
}

static bool
operator==(const StringView a, const StringView b) noexcept
{
	if (a.IsNull() || b.IsNull())
		return a.IsNull() && b.IsNull();

	if (a.size != b.size)
		return false;

	return memcmp(a.data, b.data, a.size) == 0;
}

static bool
operator==(const StringView a, const char *b) noexcept
{
	return a == StringView(b);
}

static bool
operator==(const StringView a, std::nullptr_t b) noexcept
{
	return a == StringView(b);
}

TEST(CgiAddressTest, RelativeTo)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	const auto base = MakeCgiAddress("/usr/bin/cgi", "/test.pl", "/foo/");

	ASSERT_EQ(MakeCgiAddress("/usr/bin/other-cgi", "/test.pl", "/foo/").RelativeTo(base), nullptr);

	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", "/test.pl", nullptr).RelativeTo(base), nullptr);
	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", "/test.pl", "/").RelativeTo(base), nullptr);
	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", "/test.pl", "/foo").RelativeTo(base), nullptr);
	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", "/test.pl", "/foo/").RelativeTo(base), "");
	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", "/test.pl", "/foo/bar").RelativeTo(base), "bar");
}
