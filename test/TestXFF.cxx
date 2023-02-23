// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/XForwardedFor.hxx"

#include <gtest/gtest.h>

TEST(HttpUtil, XFF)
{
	const XForwardedForConfig config{
		{"192.168.0.1", "127.0.0.1", "::1", "dead::beef", "localhost"},
	};

	EXPECT_TRUE(config.IsTrustedHost("127.0.0.1"));
	EXPECT_TRUE(config.IsTrustedHost("192.168.0.1"));
	EXPECT_TRUE(config.IsTrustedHost("::1"));
	EXPECT_TRUE(config.IsTrustedHost("dead::beef"));
	EXPECT_TRUE(config.IsTrustedHost("localhost"));
	EXPECT_FALSE(config.IsTrustedHost("127.0.0.2"));
	EXPECT_FALSE(config.IsTrustedHost("dead::bee"));

	EXPECT_EQ(config.GetRealRemoteHost(""), "");
	EXPECT_EQ(config.GetRealRemoteHost(" "), "");
	EXPECT_EQ(config.GetRealRemoteHost("foo, bar"), "bar");
	EXPECT_EQ(config.GetRealRemoteHost("foo, bar "), "bar");
	EXPECT_EQ(config.GetRealRemoteHost("foo,bar "), "bar");
	EXPECT_EQ(config.GetRealRemoteHost(" foo,bar"), "bar");
	EXPECT_EQ(config.GetRealRemoteHost(" foo,bar,localhost"), "bar");
	EXPECT_EQ(config.GetRealRemoteHost(" foo,bar, localhost  "), "bar");
	EXPECT_EQ(config.GetRealRemoteHost("foo,bar,dead::beef"), "bar");
	EXPECT_EQ(config.GetRealRemoteHost("foo,bar,127.0.0.1"), "bar");
	EXPECT_EQ(config.GetRealRemoteHost("foo,bar,192.168.0.1"), "bar");
	EXPECT_EQ(config.GetRealRemoteHost("localhost"), "localhost");
	EXPECT_EQ(config.GetRealRemoteHost(",localhost"), "localhost");
	EXPECT_EQ(config.GetRealRemoteHost(" ,localhost"), "localhost");
}
