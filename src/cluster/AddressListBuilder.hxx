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
