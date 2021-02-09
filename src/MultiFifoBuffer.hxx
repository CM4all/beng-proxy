/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "DefaultFifoBuffer.hxx"

#include <list>

#include <stdint.h>

template<typename T> struct ConstBuffer;
class IstreamBucketList;

class MultiFifoBuffer {
	std::list<DefaultFifoBuffer> buffers;

public:
	MultiFifoBuffer() noexcept;
	~MultiFifoBuffer() noexcept;

	MultiFifoBuffer(MultiFifoBuffer &&) noexcept = default;
	MultiFifoBuffer &operator=(MultiFifoBuffer &&) noexcept = default;

	[[gnu::pure]]
	bool empty() const noexcept {
		return buffers.empty();
	}

	void Push(ConstBuffer<uint8_t> src) noexcept;

	[[gnu::pure]]
	size_t GetAvailable() const noexcept;

	ConstBuffer<uint8_t> Read() const noexcept;
	void Consume(size_t nbytes) noexcept;

	void FillBucketList(IstreamBucketList &list) const noexcept;

	/**
	 * Like Consume(), but can run across several buffers and the
	 * parameter is allowed to be larger than GetAvailable().
	 */
	size_t Skip(size_t nbytes) noexcept;
};
