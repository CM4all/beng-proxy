// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Body.hxx"
#include "istream/Handler.hxx"
#include "istream/UnusedPtr.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketProtocolError.hxx"

#include <stdexcept>
#include <tuple>

#include <limits.h>

/** determine how much can be read from the body */
std::size_t
HttpBodyReader::GetMaxRead(std::size_t length) const noexcept
{
	assert(rest != REST_EOF_CHUNK);

	if (KnownLength() && std::cmp_less(rest, length))
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

	assert(std::cmp_less_equal(nbytes, rest));

	rest -= (off_t)nbytes;
}

std::size_t
HttpBodyReader::FeedBody(std::span<const std::byte> src) noexcept
{
	assert(!src.empty());

	const auto [t, then_eof] = TruncateInput(src);

	std::size_t consumed = InvokeData(t);
	if (consumed > 0)
		Consumed(consumed);

	return consumed;
}

IstreamDirectResult
HttpBodyReader::TryDirect(SocketDescriptor fd, FdType fd_type) noexcept
{
	assert(fd.IsDefined());
	assert(CheckDirect(fd_type));

	std::size_t max_size = INT_MAX;
	bool then_eof = false;
	if (KnownLength())
		std::tie(max_size, then_eof) = CalcMaxDirect(rest);

	return InvokeDirect(fd_type, fd.ToFileDescriptor(),
			    IstreamHandler::NO_OFFSET,
			    max_size, then_eof);
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
	} else if (std::cmp_equal(rest, remaining)) {
		if (remaining > 0)
			/* serve the rest of the buffer, then end the body
			   stream */
			return true;

		InvokeEof();
		return false;
	} else if ((rest == REST_CHUNKED && remaining > 0) ||
		   rest == REST_EOF_CHUNK) {
		/* suppress InvokeEof() because the dechunker is responsible
		   for that */
		return true;
	} else {
		/* something has gone wrong: either not enough or too much
		   data left in the buffer */
		InvokeError(std::make_exception_ptr(SocketClosedPrematurelyError{}));
		return false;
	}
}

void
HttpBodyReader::OnDechunkEndSeen() noexcept
{
	assert(rest == REST_CHUNKED);

	end_seen = true;
}

DechunkHandler::DechunkInputAction
HttpBodyReader::OnDechunkEnd() noexcept
{
	assert(rest == REST_CHUNKED);
	assert(end_seen);

	rest = REST_EOF_CHUNK;

	return DechunkInputAction::ABANDON;
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
