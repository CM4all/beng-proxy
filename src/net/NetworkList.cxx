// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "NetworkList.hxx"
#include "net/MaskedSocketAddress.hxx"
#include "AllocatorPtr.hxx"

#include <algorithm> // for std::any_of()

#include <netinet/in.h>
#include <string.h> // for memcpy()

class SocketAddress;

struct NetworkList::Item : IntrusiveForwardListHook
{
	uint_least8_t prefix_length;

	struct sockaddr address;

	explicit Item(SocketAddress src, uint_least8_t _prefix_length) noexcept
		:prefix_length(_prefix_length)
	{
		memcpy(&address, src.GetAddress(), src.GetSize());
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

	SocketAddress::size_type AddressSize() const noexcept {
		return AddressSize(address);
	}

	operator SocketAddress() const noexcept {
		if (const auto size = AddressSize(); size > 0)
			return {&address, size};
		else
			return nullptr;
	}

	static std::size_t ObjectSize(std::size_t address_size) noexcept {
		return sizeof(Item) - sizeof(address) + address_size;
	}

	std::size_t ObjectSize() const noexcept {
		return ObjectSize(AddressSize(address));
	}

	Item *Dup(AllocatorPtr alloc) const noexcept {
		return (Item *)alloc.Dup(this, ObjectSize());
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

	const auto address_size = Item::AddressSize(*address.GetAddress());
	if (address_size == 0)
		throw std::invalid_argument{"Unsupported address family"};

	if (address_size != address.GetSize())
		throw std::invalid_argument{"Bad address size"};

	// TODO check if prefix_length is in range
	// TODO check if all other bits are zero

	auto *item = alloc.NewVar<Item>(Item::ObjectSize(address_size), address, prefix_length);
	list.push_front(*item);
}

inline bool
NetworkList::Item::Contains(SocketAddress other) const noexcept
{
	return MaskedSocketAddress::Matches(*this, prefix_length, other);
}

bool
NetworkList::Contains(SocketAddress address) const noexcept
{
	return std::any_of(list.begin(), list.end(), [address](const auto &i){ return i.Contains(address); });
}
