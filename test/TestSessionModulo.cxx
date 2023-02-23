// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "lb/Session.hxx"
#include "bp/session/Id.hxx"
#include "cluster/StickyHash.hxx"
#include "pool/RootPool.hxx"
#include "util/StringBuffer.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <gtest/gtest.h>

static StringMap
MakeHeaders(AllocatorPtr alloc, const SessionId &id) noexcept
{
	const char *cookie = alloc.Concat("foo=", id.Format());

	return StringMap{
		alloc,
		{
			{"cookie", cookie},
		},
	};
}

static StringMap
MakeHeaders(AllocatorPtr alloc, SessionId id,
	    unsigned cluster_size, unsigned cluster_node) noexcept
{
	id.SetClusterNode(cluster_size, cluster_node);
	return MakeHeaders(alloc, id);
}

TEST(SessionModulo, Basic)
{
	SessionPrng prng;

	RootPool pool;
	const AllocatorPtr alloc{pool};

	for (unsigned cluster_size = 2; cluster_size <= 16; ++cluster_size) {
		SessionId id;
		id.Generate(prng);

		for (unsigned cluster_node = 0; cluster_node < cluster_size;
		     ++cluster_node) {
			const auto headers =
				MakeHeaders(alloc, id,
					    cluster_size, cluster_node);

			const auto hash = lb_session_get(headers, "foo");
			EXPECT_NE(hash, 0U);
			EXPECT_EQ(hash % cluster_size, cluster_node);
		}
	}
}
