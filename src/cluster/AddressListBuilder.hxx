// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "StickyMode.hxx"
#include "net/SocketAddress.hxx"

#include <vector>

class AllocatorPtr;
class AddressInfoList;
struct AddressList;

/**
 * Builder for an #AddressList.
 */
class AddressListBuilder {
	StickyMode sticky_mode = StickyMode::NONE;

	std::vector<SocketAddress> v;

public:
	bool empty() const noexcept {
		return v.empty();
	}

	void clear() noexcept {
		v.clear();
	}

	void SetStickyMode(StickyMode _sticky_mode) noexcept {
		sticky_mode = _sticky_mode;
	}

	void AddPointer(SocketAddress address) noexcept {
		v.push_back(address);
	}

	void Add(AllocatorPtr alloc, SocketAddress address) noexcept;
	void Add(AllocatorPtr alloc, const AddressInfoList &list) noexcept;

	AddressList Finish(AllocatorPtr alloc) const noexcept;
};
