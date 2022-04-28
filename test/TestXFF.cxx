/*
 * Copyright 2007-2021 CM4all GmbH
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
