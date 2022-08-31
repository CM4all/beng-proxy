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

#include "Body.hxx"
#include "istream/Handler.hxx"
#include "istream/UnusedPtr.hxx"
#include "net/SocketDescriptor.hxx"

#include <stdexcept>

#include <limits.h>

/** determine how much can be read from the body */
std::size_t
HttpBodyReader::GetMaxRead(std::size_t length) const noexcept
{
	assert(rest != REST_EOF_CHUNK);

	if (KnownLength() && rest < (off_t)length)
		/* content-length header was provided, return this value */
		return (std::size_t)rest;
	else
		/* read as much as possible, the dechunker will do the rest */
		return length;
}

void
HttpBodyReader::Consumed(std::size_t nbytes) noexcept
{
	if (!KnownLength())
		return;

	assert((off_t)nbytes <= rest);

	rest -= (off_t)nbytes;
}

std::size_t
HttpBodyReader::FeedBody(std::span<const std::byte> src) noexcept
{
	assert(!src.empty());

	src = src.first(GetMaxRead(src.size()));
	std::size_t consumed = InvokeData(src);
	if (consumed > 0)
		Consumed(consumed);

	return consumed;
}

IstreamDirectResult
HttpBodyReader::TryDirect(SocketDescriptor fd, FdType fd_type) noexcept
{
	assert(fd.IsDefined());
	assert(CheckDirect(fd_type));

	return InvokeDirect(fd_type, fd.ToFileDescriptor(),
			    IstreamHandler::NO_OFFSET,
			    GetMaxRead(INT_MAX));
}

bool
HttpBodyReader::SocketEOF(std::size_t remaining) noexcept
{
	if (rest == REST_UNKNOWN) {
		rest = remaining;

		if (remaining > 0) {
			/* serve the rest of the buffer, then end the body
			   stream */
			return true;
		}

		/* the socket is closed, which ends the body */
		InvokeEof();
		return false;
	} else if (rest == REST_CHUNKED ||
		   rest == REST_EOF_CHUNK) {
		/* suppress InvokeEof() because the dechunker is responsible
		   for that */
		return remaining > 0;
	} else if (rest == (off_t)remaining) {
		if (remaining > 0)
			/* serve the rest of the buffer, then end the body
			   stream */
			return true;

		InvokeEof();
		return false;
	} else {
		/* something has gone wrong: either not enough or too much
		   data left in the buffer */
		InvokeError(std::make_exception_ptr(std::runtime_error("premature end of socket")));
		return false;
	}
}

void
HttpBodyReader::OnDechunkEndSeen() noexcept
{
	assert(rest == REST_CHUNKED);

	end_seen = true;
}

bool
HttpBodyReader::OnDechunkEnd() noexcept
{
	assert(rest == REST_CHUNKED);

	end_seen = true;
	rest = REST_EOF_CHUNK;

	return true;
}

UnusedIstreamPtr
HttpBodyReader::Init(EventLoop &event_loop, off_t content_length,
		     bool _chunked) noexcept
{
	assert(content_length >= -1);

	rest = content_length;

	UnusedIstreamPtr s(this);
	if (_chunked) {
		assert(rest == (off_t)REST_UNKNOWN);

		rest = REST_CHUNKED;
		end_seen = false;

		s = istream_dechunk_new(GetPool(), std::move(s),
					event_loop, *this);
	}

	return s;
}
