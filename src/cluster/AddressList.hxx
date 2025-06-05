// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "StickyMode.hxx"
#include "net/SocketAddress.hxx"
#include "util/TagStructs.hxx"

#include <cassert>
#include <span>

class AllocatorPtr;
class AddressInfoList;

/**
 * Store a URI along with a list of socket addresses.
 */
struct AddressList {
	StickyMode sticky_mode = StickyMode::NONE;

	using Array = std::span<const SocketAddress>;
	using size_type = Array::size_type;
	using const_iterator = Array::iterator;
	using const_reference = Array::const_reference;

	Array addresses;

	AddressList() = default;

	constexpr AddressList(ShallowCopy, StickyMode _sticky_mode,
			      std::span<const SocketAddress> src) noexcept
		:sticky_mode(_sticky_mode),
		 addresses(src.begin(), src.end())
	{
	}

	constexpr AddressList(ShallowCopy, const AddressList &src) noexcept
		:sticky_mode(src.sticky_mode),
		 addresses(src.addresses)
	{
	}

	AddressList(AllocatorPtr alloc, const AddressList &src) noexcept;

	constexpr
	bool empty() const noexcept {
		return addresses.empty();
	}

	constexpr size_type size() const noexcept {
		return addresses.size();
	}

	/**
	 * Is there no more than one address?
	 */
	constexpr
	bool IsSingle() const noexcept {
		return addresses.size() == 1;
	}

	constexpr const_iterator begin() const noexcept {
		return addresses.begin();
	}

	constexpr const_iterator end() const noexcept {
		return addresses.end();
	}

	constexpr const_reference front() const noexcept {
		return addresses.front();
	}

	const SocketAddress &operator[](unsigned n) const noexcept {
		assert(addresses[n].IsDefined());

		return addresses[n];
	}
};
