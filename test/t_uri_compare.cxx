// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "uri/Compare.hxx"

#include <gtest/gtest.h>

TEST(UriCompareTest, UriFindUnescapedSuffix)
{
	const char *uri1 = "/foo";
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "bar"), nullptr);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "foo"), uri1 + 1);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "/foo"), uri1);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, " /foo"), nullptr);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "oo"), uri1 + 2);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "%6fo"), uri1 + 2);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "%6f%6f"), uri1 + 2);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "%66%6f%6f"), uri1 + 1);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "%2f%66%6f%6f"), uri1);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "%6f%6"), nullptr);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "%6f%"), nullptr);
	EXPECT_EQ(UriFindUnescapedSuffix(uri1, "%%6f"), nullptr);
}
