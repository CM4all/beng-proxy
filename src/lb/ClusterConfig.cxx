// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ClusterConfig.hxx"

#include <stdexcept>

void
LbClusterConfig::FillAddressList()
{
	assert(address_list.empty());

	address_list_allocation = std::make_unique<SocketAddress[]>(members.size());

	const unsigned default_port = GetDefaultPort();

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
