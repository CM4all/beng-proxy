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

#include "GrowingBuffer.hxx"
#include "istream/Sink.hxx"

class GrowingBufferSinkHandler {
public:
	virtual void OnGrowingBufferSinkEof(GrowingBuffer buffer) noexcept = 0;
	virtual void OnGrowingBufferSinkError(std::exception_ptr error) noexcept = 0;
};

/**
 * An #IstreamSink implementation which copies data into a #GrowingBuffer.
 */
class GrowingBufferSink final : public IstreamSink {
	GrowingBuffer buffer;

	GrowingBufferSinkHandler &handler;

public:
	template<typename I>
	GrowingBufferSink(I &&_input, GrowingBufferSinkHandler &_handler) noexcept
		:IstreamSink(std::forward<I>(_input)),
		 handler(_handler)
	{
		input.SetDirect(FD_ANY);
	}

	void Read() noexcept {
		input.Read();
	}

protected:
	/* virtual methods from class IstreamHandler */
	bool OnIstreamReady() noexcept override;
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     std::size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr error) noexcept override;
};
