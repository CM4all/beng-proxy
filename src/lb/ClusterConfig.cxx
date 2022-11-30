/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "ClusterConfig.hxx"

#include <stdexcept>

void
LbClusterConfig::FillAddressList()
{
	assert(address_list.empty());

	address_list_allocation = std::make_unique<SocketAddress[]>(members.size());

	const unsigned default_port = GetDefaultPort(protocol);

	auto *p = address_list_allocation.get();
	for (auto &member : members) {
		address_allocations.emplace_front(member.node->address);
		auto &address = address_allocations.front();
		if (member.port != 0)
			address.SetPort(member.port);
		else if (default_port > 0 && address.GetPort() == 0)
			address.SetPort(default_port);

		*p++ = address;
	}

	address_list = AddressList{
		ShallowCopy{},
		sticky_mode,
		std::span<const SocketAddress>{address_list_allocation.get(), members.size()},
	};
}

int
LbClusterConfig::FindJVMRoute(std::string_view jvm_route) const noexcept
{
	for (unsigned i = 0, n = members.size(); i < n; ++i) {
		const auto &node = *members[i].node;

		if (!node.jvm_route.empty() && node.jvm_route == jvm_route)
			return i;
	}

	return -1;
}
