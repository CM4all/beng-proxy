// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "uri/Args.hxx"
#include "strmap.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

#include <string.h>

using std::string_view_literals::operator""sv;

TEST(Args, Parse)
{
	RootPool pool;
	AllocatorPtr alloc(pool);

	const std::string_view s = "a=foo&b=bar&c=$20&=&=xyz&d=&e"sv;
	const auto args = args_parse(alloc, s);

	EXPECT_EQ(std::distance(args.begin(), args.end()), 4u);
	EXPECT_STREQ(args.Get("a"), "foo");
	EXPECT_STREQ(args.Get("b"), "bar");
	EXPECT_STREQ(args.Get("c"), " ");
	EXPECT_STREQ(args.Get("d"), "");
}
