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

#include "lb/Session.hxx"
#include "bp/session/Id.hxx"
#include "cluster/StickyHash.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"
#include "random.hxx"
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
	random_seed();

	RootPool pool;
	const AllocatorPtr alloc{pool};

	for (unsigned cluster_size = 2; cluster_size <= 16; ++cluster_size) {
		SessionId id;
		id.Generate();

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
