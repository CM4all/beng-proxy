// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "bp/session/Id.hxx"
#include "bp/session/Prng.hxx"
#include "util/StringBuffer.hxx"

#include <gtest/gtest.h>

TEST(SessionIdTest, IsDefined)
{
	SessionPrng prng;

	SessionId a;
	a.Clear();
	EXPECT_FALSE(a.IsDefined());
	EXPECT_EQ(a, a);

	SessionId b;
	b.Generate(prng);
	EXPECT_TRUE(b.IsDefined());
	EXPECT_EQ(b, b);
	EXPECT_NE(a, b);
	EXPECT_NE(b, a);
}

TEST(SessionIdTest, FormatAndParse)
{
	SessionPrng prng;

	SessionId a;
	a.Generate(prng);
	EXPECT_TRUE(a.IsDefined());

	const auto s = a.Format();
	EXPECT_EQ(strlen(s), sizeof(a) * 2);

	SessionId b;
	ASSERT_TRUE(b.Parse(s.c_str()));
	ASSERT_EQ(b, a);
	ASSERT_EQ(a, b);
}

TEST(SessionIdTest, ClusterHash)
{
	SessionPrng prng;

	for (unsigned cluster_size = 2; cluster_size <= 16; ++cluster_size) {
		for (unsigned cluster_node = 0; cluster_node < cluster_size; ++cluster_node) {
			SessionId a;
			a.Generate(prng);
			EXPECT_TRUE(a.IsDefined());

			a.SetClusterNode(cluster_size, cluster_node);
			ASSERT_EQ(a.GetClusterHash() % cluster_size, cluster_node);
		}
	}
}
