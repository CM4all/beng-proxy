// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "NetworkList.hxx"
#include "net/InetAddress.hxx"
#include "net/MaskedSocketAddress.hxx"
#include "AllocatorPtr.hxx"

#include <algorithm> // for std::any_of()

#include <netinet/in.h>
#include <string.h> // for memcpy()

class SocketAddress;

struct NetworkList::Item : IntrusiveForwardListHook
{
	uint_least8_t prefix_length;

	InetAddress address;

	explicit Item(const auto &src, uint_least8_t _prefix_length) noexcept
		:prefix_length(_prefix_length), address(src)
	{
	}

	static SocketAddress::size_type AddressSize(const struct sockaddr &address) noexcept {
		switch (address.sa_family) {
		case AF_INET:
			return sizeof(struct sockaddr_in);

		case AF_INET6:
			return sizeof(struct sockaddr_in6);

		default:
			return 0;
		}
	}

	Item *Dup(AllocatorPtr alloc) const noexcept {
		return alloc.New<Item>(address, prefix_length);
	}

	[[gnu::pure]]
	bool Contains(SocketAddress other) const noexcept;
};

NetworkList::NetworkList(AllocatorPtr alloc, const NetworkList &src) noexcept
{
	auto it = list.before_begin();
	for (const auto &src_item : src.list) {
		auto *dest_item = src_item.Dup(alloc);
		it = list.insert_after(it, *dest_item);
	}
}

void
NetworkList::Add(AllocatorPtr alloc, SocketAddress address, uint_least8_t prefix_length)
{
	assert(!address.IsNull());

	if (prefix_length > MaskedSocketAddress::MaximumPrefixLength(address))
		throw std::invalid_argument{"Bad network prefix length"};

	Item *item;
	switch (address.GetFamily()) {
	case AF_INET:
		item = alloc.New<Item>(IPv4Address::Cast(address), prefix_length);
		break;

	case AF_INET6:
		item = alloc.New<Item>(IPv6Address::Cast(address), prefix_length);
		break;

	default:
		throw std::invalid_argument{"Unsupported address family"};
	}

	list.push_front(*item);
}

inline bool
NetworkList::Item::Contains(SocketAddress other) const noexcept
{
	return MaskedSocketAddress::Matches(address, prefix_length, other);
}

bool
NetworkList::Contains(SocketAddress address) const noexcept
{
	return std::any_of(list.begin(), list.end(), [address](const auto &i){ return i.Contains(address); });
}
