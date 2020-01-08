/*
 * Copyright 2007-2018 Content Management AG
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

#include <exception>

#include <stdint.h>

struct pool;
class EventLoop;
class FileDescriptor;
class UnusedIstreamPtr;
class WasOutput;

class WasOutputHandler {
public:
	/**
	 * Announces the length of the resource.
	 *
	 * @param true on success, false if the #WasOutput object has been
	 * deleted
	 */
	virtual bool WasOutputLength(uint64_t length) noexcept = 0;

	/**
	 * The stream ended prematurely, but the #WasOutput object is
	 * still ok.
	 *
	 * @param the number of bytes aready sent
	 * @param true on success, false if the #WasOutput object has been
	 * deleted
	 */
	virtual bool WasOutputPremature(uint64_t length,
					std::exception_ptr ep) noexcept = 0;

	virtual void WasOutputEof() noexcept = 0;

	virtual void WasOutputError(std::exception_ptr ep) noexcept = 0;
};

/**
 * Web Application Socket protocol, output data channel library.
 */
WasOutput *
was_output_new(struct pool &pool, EventLoop &event_loop,
	       FileDescriptor fd, UnusedIstreamPtr input,
	       WasOutputHandler &handler) noexcept;

/**
 * @return the total number of bytes written to the pipe
 */
uint64_t
was_output_free(WasOutput *data) noexcept;

static inline uint64_t
was_output_free_p(WasOutput **output_p) noexcept
{
	WasOutput *output = *output_p;
	*output_p = nullptr;
	return was_output_free(output);
}

/**
 * Check if we can provide the LENGTH header.
 *
 * @return the WasOutputHandler::length() return value
 */
bool
was_output_check_length(WasOutput &output) noexcept;
