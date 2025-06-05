// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "lib/pcre/UniqueRegex.hxx"
#include "pexpand.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>

TEST(RegexTest, Expand)
{
	UniqueRegex r;
	ASSERT_FALSE(r.IsDefined());
	r.Compile("^/foo/(\\w+)/([^/]+)/(.*)$", {.capture=true});
	ASSERT_TRUE(r.IsDefined());

	ASSERT_FALSE(r.Match("a"));

	auto match_data = r.Match("/foo/bar/a/b/c.html");
	ASSERT_TRUE(match_data);

	TestPool pool;
	AllocatorPtr alloc(pool);

	auto e = expand_string(alloc, "\\1-\\2-\\3-\\\\", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "bar-a-b/c.html-\\"), 0);

	match_data = r.Match("/foo/bar/a/b/");
	ASSERT_TRUE(match_data);

	e = expand_string(alloc, "\\1-\\2-\\3-\\\\", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "bar-a-b/-\\"), 0);

	match_data = r.Match("/foo/bar/a%20b/c%2520.html");
	ASSERT_TRUE(match_data);

	e = expand_string_unescaped(alloc, "\\1-\\2-\\3", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "bar-a b-c%20.html"), 0);

	ASSERT_THROW(expand_string_unescaped(alloc, "\\4", match_data),
		     std::runtime_error);
}

TEST(RegexTest, ExpandMalformedUriEscape)
{
	UniqueRegex r;
	ASSERT_FALSE(r.IsDefined());
	r.Compile("^(.*)$", {.capture=true});
	ASSERT_TRUE(r.IsDefined());

	auto match_data = r.Match("%xxx");
	ASSERT_TRUE(match_data);

	TestPool pool;
	AllocatorPtr alloc(pool);

	auto e = expand_string(alloc, "-\\1-", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "-%xxx-"), 0);

	ASSERT_THROW(expand_string_unescaped(alloc, "-\\1-", match_data),
		     std::runtime_error);
}

TEST(RegexTest, ExpandOptional)
{
	UniqueRegex r;
	ASSERT_FALSE(r.IsDefined());
	r.Compile("^(a)(b)?(c)$", {.anchored=true, .capture=true});
	ASSERT_TRUE(r.IsDefined());

	auto match_data = r.Match("abc");
	ASSERT_TRUE(match_data);

	TestPool pool;
	AllocatorPtr alloc(pool);

	auto e = expand_string(alloc, "\\1-\\2-\\3", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "a-b-c"), 0);

	match_data = r.Match("ac");
	ASSERT_TRUE(match_data);
	e = expand_string(alloc, "\\1-\\2-\\3", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "a--c"), 0);
}

TEST(RegexTest, ExpandOptionalLast)
{
	UniqueRegex r;
	ASSERT_FALSE(r.IsDefined());
	r.Compile("^(a)(b)?(c)?$", {.anchored=true, .capture=true});
	ASSERT_TRUE(r.IsDefined());

	auto match_data = r.Match("abc");
	ASSERT_TRUE(match_data);

	TestPool pool;
	AllocatorPtr alloc(pool);

	auto e = expand_string(alloc, "\\1-\\2-\\3", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "a-b-c"), 0);

	match_data = r.Match("ac");
	ASSERT_TRUE(match_data);
	e = expand_string(alloc, "\\1-\\2-\\3", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "a--c"), 0);

	match_data = r.Match("ab");
	ASSERT_TRUE(match_data);
	e = expand_string(alloc, "\\1-\\2-\\3", match_data);
	ASSERT_NE(e, nullptr);
	ASSERT_EQ(strcmp(e, "a-b-"), 0);
}
