/*
 * Copyright 2007-2017 Content Management AG
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

#include "net/SocketAddress.hxx"
#include "util/StaticArray.hxx"
#include "util/ShallowCopy.hxx"
#include "StickyMode.hxx"

#include "util/Compiler.h"

#include <stddef.h>
#include <assert.h>

struct dpool;
class AllocatorPtr;
class SocketAddress;
class AddressInfoList;

/**
 * Store a URI along with a list of socket addresses.
 */
struct AddressList {
	static constexpr size_t MAX_ADDRESSES = 16;

	StickyMode sticky_mode = StickyMode::NONE;

	typedef StaticArray<SocketAddress, MAX_ADDRESSES> Array;
	typedef Array::const_iterator const_iterator;

	Array addresses;

	AddressList() = default;

	constexpr AddressList(ShallowCopy, const AddressList &src) noexcept
		:sticky_mode(src.sticky_mode),
		 addresses(src.addresses)
	{
	}

	AddressList(ShallowCopy, const AddressInfoList &src) noexcept;

	AddressList(AllocatorPtr alloc, const AddressList &src) noexcept;

	AddressList(struct dpool &pool, const AddressList &src) noexcept;
	void Free(struct dpool &pool) noexcept;

	void SetStickyMode(StickyMode _sticky_mode) noexcept {
		sticky_mode = _sticky_mode;
	}

	constexpr
	bool IsEmpty() const noexcept {
		return addresses.empty();
	}

	Array::size_type GetSize() const noexcept {
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

	/**
	 * @return false if the list is full
	 */
	bool AddPointer(SocketAddress address) noexcept {
		return addresses.checked_append(address);
	}

	bool Add(AllocatorPtr alloc, SocketAddress address) noexcept;
	bool Add(AllocatorPtr alloc, const AddressInfoList &list) noexcept;

	bool Add(struct dpool &pool, SocketAddress address) noexcept;

	const SocketAddress &operator[](unsigned n) const noexcept {
		assert(addresses[n].IsDefined());

		return addresses[n];
	}

	/**
	 * Generates a unique string which identifies this object in a hash
	 * table.  This string stored in a statically allocated buffer.
	 */
	gcc_pure
	const char *GetKey() const noexcept;
};
