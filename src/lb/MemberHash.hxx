// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "cluster/StickyHash.hxx"
#include "util/HashRing.hxx"

#include <cstddef>

class SocketAddress;

[[gnu::pure]]
sticky_hash_t
MemberAddressHash(SocketAddress address, std::size_t replica) noexcept;

template<typename Node>
using MemberHashRing = HashRing<Node, sticky_hash_t, 8192, 64>;

template<typename Node, typename C, typename F>
void
BuildMemberHashRing(MemberHashRing<Node> &ring, C &&nodes, F &&f) noexcept
{
	ring.Build(std::forward<C>(nodes),
		   [&f](const Node &node, std::size_t replica) noexcept {
			   return MemberAddressHash(f(node), replica);
		   });
}
