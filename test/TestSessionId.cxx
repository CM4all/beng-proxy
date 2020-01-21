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

#include "bp/session/Id.hxx"
#include "random.hxx"

#include <gtest/gtest.h>

TEST(SessionIdTest, IsDefined)
{
	random_seed();

	SessionId a;
	a.Clear();
	EXPECT_FALSE(a.IsDefined());
	EXPECT_EQ(a, a);

	SessionId b;
	b.Generate();
	EXPECT_TRUE(b.IsDefined());
	EXPECT_EQ(b, b);
	EXPECT_NE(a, b);
	EXPECT_NE(b, a);
}

TEST(SessionIdTest, FormatAndParse)
{
	random_seed();

	SessionId a;
	a.Generate();
	EXPECT_TRUE(a.IsDefined());

	const auto s = a.Format();
	EXPECT_EQ(strlen(s), sizeof(a) * 2);

	SessionId b;
	ASSERT_TRUE(b.Parse(s));
	ASSERT_EQ(b, a);
	ASSERT_EQ(a, b);
}

TEST(SessionIdTest, ClusterHash)
{
	random_seed();

	for (unsigned cluster_size = 2; cluster_size <= 16; ++cluster_size) {
		for (unsigned cluster_node = 0; cluster_node < cluster_size; ++cluster_node) {
			SessionId a;
			a.Generate();
			EXPECT_TRUE(a.IsDefined());

			a.SetClusterNode(cluster_size, cluster_node);
			ASSERT_EQ(a.GetClusterHash() % cluster_size, cluster_node);
		}
	}
}
