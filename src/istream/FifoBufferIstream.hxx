/*
 * Copyright 2007-2019 Content Management AG
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

#include "pool/SharedPtr.hxx"

#include <utility>

class SliceFifoBuffer;
class UnusedIstreamPtr;
class FifoBufferIstream;

class FifoBufferIstreamHandler {
public:
	virtual void OnFifoBufferIstreamDrained() noexcept = 0;
	virtual void OnFifoBufferIstreamClosed() noexcept {}
};

class FifoBufferIstreamControl final {
	friend class FifoBufferIstream;

	FifoBufferIstream *fbi;

public:
	explicit constexpr FifoBufferIstreamControl(FifoBufferIstream &_fbi) noexcept
		:fbi(&_fbi) {}

	operator bool() const noexcept {
		return fbi != nullptr;
	}

	SliceFifoBuffer *GetBuffer() noexcept;

	size_t Push(ConstBuffer<void> src) noexcept;

	void SubmitBuffer() noexcept;

	void SetEof() noexcept;
	void DestroyError(std::exception_ptr e) noexcept;
};

/**
 * An #Istream implementation which serves data from a FIFO buffer.
 */
std::pair<UnusedIstreamPtr, SharedPoolPtr<FifoBufferIstreamControl>>
NewFifoBufferIstream(struct pool &pool,
		     FifoBufferIstreamHandler &handler) noexcept;
