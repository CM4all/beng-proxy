// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/IntrusiveForwardList.hxx"
#include "util/TagStructs.hxx"

#include <cstdint>

class AllocatorPtr;
class SocketAddress;

class NetworkList final {
	struct Item;
	IntrusiveForwardList<Item> list;

public:
	NetworkList() noexcept = default;

	NetworkList(ShallowCopy shallow_copy, const NetworkList &src) noexcept
		:list(shallow_copy, src.list) {}

	NetworkList(AllocatorPtr alloc, const NetworkList &src) noexcept;

	bool empty() const noexcept {
		return list.empty();
	}

	void clear() noexcept {
		list.clear();
	}

	/**
	 * Throws if the specified network address is malformed.
	 */
	void Add(AllocatorPtr alloc, SocketAddress address, uint_least8_t prefix_length);

	[[gnu::pure]]
	bool Contains(SocketAddress address) const noexcept;
};
