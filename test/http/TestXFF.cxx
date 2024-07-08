// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/XForwardedFor.hxx"
#include "net/Parser.hxx"
#include "net/Literals.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

TEST(HttpUtil, XFF)
{
	const XForwardedForConfig config{
		{"192.168.0.1", "127.0.0.1", "::1", "dead::beef", "localhost"},
		{
			MaskedSocketAddress{"c0ff:ee::/32"},
			MaskedSocketAddress{"10.42.0.0/16"},
			MaskedSocketAddress{"192.168.128.0/18"},
		},
	};

	EXPECT_TRUE(config.IsTrustedHost("127.0.0.1"sv));
	EXPECT_TRUE(config.IsTrustedHost("192.168.0.1"sv));
	EXPECT_TRUE(config.IsTrustedHost("::1"sv));
	EXPECT_TRUE(config.IsTrustedHost("dead::beef"sv));
	EXPECT_TRUE(config.IsTrustedHost("localhost"sv));
	EXPECT_FALSE(config.IsTrustedHost("127.0.0.2"sv));
	EXPECT_FALSE(config.IsTrustedHost("dead::bee"sv));

	EXPECT_TRUE(config.IsTrustedAddress(ParseSocketAddress("c0ff:ee::", 0, true)));
	EXPECT_TRUE(config.IsTrustedAddress(ParseSocketAddress("c0ff:ee::1", 0, true)));
	EXPECT_TRUE(config.IsTrustedAddress(ParseSocketAddress("c0ff:ee:ffff::", 0, true)));
	EXPECT_TRUE(config.IsTrustedAddress(ParseSocketAddress("c0ff:00ee:ffff::", 0, true)));
	EXPECT_TRUE(config.IsTrustedAddress(ParseSocketAddress("c0ff:00ee:ffff:ffff:ffff:ffff:ffff:ffff", 0, true)));
	EXPECT_FALSE(config.IsTrustedAddress(ParseSocketAddress("c0ff:1ee:ffff::", 0, true)));
	EXPECT_FALSE(config.IsTrustedAddress(ParseSocketAddress("c0ff:ee0:ffff::", 0, true)));
	EXPECT_FALSE(config.IsTrustedAddress(ParseSocketAddress("::", 0, true)));
	EXPECT_FALSE(config.IsTrustedAddress(ParseSocketAddress("::1", 0, true)));

	EXPECT_TRUE(config.IsTrustedAddress("10.42.0.0"_ipv4));
	EXPECT_TRUE(config.IsTrustedAddress("10.42.255.255"_ipv4));
	EXPECT_FALSE(config.IsTrustedAddress("10.0.0.0"_ipv4));
	EXPECT_FALSE(config.IsTrustedAddress("10.41.0.0"_ipv4));
	EXPECT_FALSE(config.IsTrustedAddress("10.43.0.0"_ipv4));
	EXPECT_FALSE(config.IsTrustedAddress("127.0.0.1"_ipv4));

	EXPECT_TRUE(config.IsTrustedAddress("192.168.128.255"_ipv4));
	EXPECT_TRUE(config.IsTrustedAddress("192.168.129.1"_ipv4));
	EXPECT_TRUE(config.IsTrustedAddress("192.168.191.1"_ipv4));
	EXPECT_TRUE(config.IsTrustedAddress("192.168.191.255"_ipv4));
	EXPECT_FALSE(config.IsTrustedAddress("192.168.192.1"_ipv4));
	EXPECT_FALSE(config.IsTrustedAddress("192.169.0.0"_ipv4));
	EXPECT_FALSE(config.IsTrustedAddress("192.168.127.1"_ipv4));
	EXPECT_FALSE(config.IsTrustedAddress("192.168.0.1"_ipv4));

	EXPECT_EQ(config.GetRealRemoteHost(""sv), ""sv);
	EXPECT_EQ(config.GetRealRemoteHost(" "sv), ""sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, bar"sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, bar "sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo,bar "sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost(" foo,bar"sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost(" foo,bar,localhost"sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost(" foo,bar, localhost  "sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo,bar,dead::beef"sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo,bar,127.0.0.1"sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo,bar,192.168.0.1"sv), "bar"sv);
	EXPECT_EQ(config.GetRealRemoteHost("localhost"sv), "localhost"sv);
	EXPECT_EQ(config.GetRealRemoteHost(",localhost"sv), "localhost"sv);
	EXPECT_EQ(config.GetRealRemoteHost(" ,localhost"sv), "localhost"sv);

	/* trust_networks in XFF header */
	EXPECT_EQ(config.GetRealRemoteHost("foo, c0ff:ef::1"sv), "c0ff:ef::1"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, c0ff:ee::1"sv), "foo"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, c0ff:ee:1:2:3:4:5:6"sv), "foo"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, c0ff:ee:fff1:fff2:fff3:fff4:fff5:fff6"sv), "foo"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, [c0ff:ee::1]"sv), "foo"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, [c0ff:ee::1]:1234"sv), "foo"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, 10.41.0.0"sv), "10.41.0.0"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, 10.42.0.1"sv), "foo"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, 10.42.0.1:0"sv), "foo"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, 10.42.0.1:1234"sv), "foo"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, 10.42.0.256"sv), "10.42.0.256"sv);
	EXPECT_EQ(config.GetRealRemoteHost("foo, 10.42.255.255"sv), "foo"sv);
}
