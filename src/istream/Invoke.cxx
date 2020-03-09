/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "istream.hxx"
#include "Handler.hxx"

#include <assert.h>

bool
Istream::InvokeReady() noexcept
{
	assert(!destroyed);
	assert(handler != nullptr);
	assert(!in_data);
	assert(!eof);
	assert(!closing);

#ifndef NDEBUG
	const DestructObserver destructed(*this);
#endif

	bool result = handler->OnIstreamReady();

#ifndef NDEBUG
	if (destructed || destroyed) {
		assert(!result);
	}
#endif

	return result;
}

size_t
Istream::InvokeData(const void *data, size_t length) noexcept
{
	assert(!destroyed);
	assert(handler != nullptr);
	assert(data != nullptr);
	assert(length > 0);
	assert(!in_data);
	assert(!eof);
	assert(!closing);
	assert(length >= data_available);
	assert(!available_full_set ||
	       (off_t)length <= available_full);

#ifndef NDEBUG
	const DestructObserver destructed(*this);
	in_data = true;
#endif

	size_t nbytes = handler->OnData(data, length);
	assert(nbytes <= length);
	assert(nbytes == 0 || !eof);

#ifndef NDEBUG
	if (destructed || destroyed) {
		assert(nbytes == 0);
		return nbytes;
	}

	in_data = false;

	if (nbytes > 0)
		Consumed(nbytes);

	data_available = length - nbytes;
#endif

	return nbytes;
}

ssize_t
Istream::InvokeDirect(FdType type, int fd, size_t max_length) noexcept
{
	assert(!destroyed);
	assert(handler != nullptr);
	assert((handler_direct & type) == type);
	assert(fd >= 0);
	assert(max_length > 0);
	assert(!in_data);
	assert(!eof);
	assert(!closing);

#ifndef NDEBUG
	const DestructObserver destructed(*this);
	in_data = true;
#endif

	ssize_t nbytes = handler->OnDirect(type, fd, max_length);
	assert(nbytes >= -3);
	assert(nbytes < 0 || (size_t)nbytes <= max_length);
	assert(nbytes == ISTREAM_RESULT_CLOSED || !eof);

#ifndef NDEBUG
	if (destructed || destroyed) {
		assert(nbytes == ISTREAM_RESULT_CLOSED);
		return nbytes;
	}

	assert(nbytes != ISTREAM_RESULT_CLOSED);

	in_data = false;

	if (nbytes > 0)
		Consumed(nbytes);
#endif

	return nbytes;
}

IstreamHandler &
Istream::PrepareEof() noexcept
{
	assert(!destroyed);
	assert(!eof);
	assert(!closing);
	assert(data_available == 0);
	assert(available_partial == 0);
	assert(!available_full_set || available_full == 0);
	assert(handler != nullptr);

#ifndef NDEBUG
	eof = true;
#endif

	return *handler;
}

void
Istream::InvokeEof() noexcept
{
	PrepareEof().OnEof();
}

IstreamHandler &
Istream::PrepareError() noexcept
{
	assert(!destroyed);
	assert(!eof);
	assert(!closing);
	assert(handler != nullptr);

#ifndef NDEBUG
	eof = true;
#endif

	return *handler;
}

void
Istream::InvokeError(std::exception_ptr ep) noexcept
{
	assert(ep);

	PrepareError().OnError(ep);
}
