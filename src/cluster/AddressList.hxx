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

#pragma once

#include "StickyMode.hxx"
#include "net/SocketAddress.hxx"
#include "util/ShallowCopy.hxx"

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

	AddressList(ShallowCopy, AllocatorPtr alloc,
		    const AddressInfoList &src) noexcept;

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
