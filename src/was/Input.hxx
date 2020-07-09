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

#include "util/Compiler.h"

#include <exception>

#include <stdint.h>

struct pool;
class FileDescriptor;
class EventLoop;
class UnusedIstreamPtr;
class WasInput;

class WasInputHandler {
public:
	/**
	 * Istream::Close() has been called.
	 *
	 * The #Istream will be destroyed right after returning from this
	 * method; the method should abandon all pointers to it, and not
	 * call it.
	 *
	 * @param received the number of bytes received so far (includes
	 * data that hasn't been delivered to the #IstreamHandler yet)
	 */
	virtual void WasInputClose(uint64_t received) noexcept = 0;

	/**
	 * All data was received from the pipe to the input buffer; we
	 * don't need the pipe anymore for this request.
	 *
	 * @return false if the #WasInput has been destroyed by this
	 * method
	 */
	virtual bool WasInputRelease() noexcept = 0;

	/**
	 * Called right before reporting end-of-file to the #IstreamHandler.
	 *
	 * The #Istream will be destroyed right after returning from this
	 * method; the method should abandon all pointers to it, and not
	 * call it.
	 */
	virtual void WasInputEof() noexcept = 0;

	/**
	 * There was an I/O error on the pipe.  Called right before
	 * reporting the error to the #IstreamHandler.
	 *
	 * The #Istream will be destroyed right after returning from this
	 * method; the method should abandon all pointers to it, and not
	 * call it.
	 */
	virtual void WasInputError() noexcept = 0;
};


/**
 * Web Application Socket protocol, input data channel library.
 */
WasInput *
was_input_new(struct pool &pool, EventLoop &event_loop, FileDescriptor fd,
	      WasInputHandler &handler) noexcept;

/**
 * @param error the error reported to the istream handler
 */
void
was_input_free(WasInput *input, std::exception_ptr ep) noexcept;

static inline void
was_input_free_p(WasInput **input_p, std::exception_ptr ep) noexcept
{
	WasInput *input = *input_p;
	*input_p = nullptr;
	was_input_free(input, ep);
}

/**
 * Like was_input_free(), but assumes that was_input_enable() has not
 * been called yet (no istream handler).
 */
void
was_input_free_unused(WasInput *input) noexcept;

static inline void
was_input_free_unused_p(WasInput **input_p) noexcept
{
	WasInput *input = *input_p;
	*input_p = nullptr;
	was_input_free_unused(input);
}

UnusedIstreamPtr
was_input_enable(WasInput &input) noexcept;

/**
 * Cancel the SocketEvent.  This is sometimes necessary, because the
 * destructor may do it too late (after the pipe lease has been
 * released already).
 */
void
was_input_disable(WasInput &input) noexcept;

/**
 * Set the new content length of this entity.
 *
 * @return false if the value is invalid (callback "abort" has been
 * invoked in this case)
 */
bool
was_input_set_length(WasInput *input, uint64_t length) noexcept;

/**
 * Signals premature end of this stream.  This method attempts to
 * recover and invokes the appropriate WasInputHandler method
 * (WasInputRelease()+WasInputEof() or WasInputError()).
 *
 * @param length the total number of bytes the peer has written to the
 * pipe
 */
void
was_input_premature(WasInput *input, uint64_t length) noexcept;

/**
 * Same as above, but throw exception instead of reporting the error
 * to the #IstreamHandler.
 */
gcc_noreturn
void
was_input_premature_throw(WasInput *input, uint64_t length);

void
was_input_enable_timeout(WasInput *input) noexcept;
