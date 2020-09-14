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

#include "Sink.hxx"
#include "SliceFifoBuffer.hxx"

class FifoBufferSinkHandler {
public:
	virtual bool OnFifoBufferSinkData() noexcept = 0;
	virtual void OnFifoBufferSinkEof() noexcept = 0;
	virtual void OnFifoBufferSinkError(std::exception_ptr ep) noexcept = 0;
};

/**
 * An #IstreamSink implementation which copies data into a FIFO buffer.
 */
class FifoBufferSink final : public IstreamSink {
	SliceFifoBuffer buffer;

	FifoBufferSinkHandler &handler;

public:
	template<typename I>
	FifoBufferSink(I &&_input, FifoBufferSinkHandler &_handler) noexcept
		:IstreamSink(std::forward<I>(_input)),
		 handler(_handler)
	{
		input.SetDirect(FD_ANY);
	}

	auto &GetBuffer() noexcept {
		return buffer;
	}

	void Read() noexcept {
		input.Read();
	}

protected:
	/* virtual methods from class IstreamHandler */
	bool OnIstreamReady() noexcept override;
	size_t OnData(const void *data, size_t length) noexcept override;
	ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};
