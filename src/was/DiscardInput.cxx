// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "DiscardInput.hxx"
#include "system/Error.hxx"
#include "net/SocketProtocolError.hxx"
#include "io/FileDescriptor.hxx"
#include "util/Exception.hxx" // for NestException()

#include <array>

/**
 * The maximum number of bytes we are willing to discard after a
 * PREMATURE packet; the peer chooses this number and discarding
 * happens synchronously, so it must not be unbounded.  This is twice
 * the maximum pipe capacity (/proc/sys/fs/pipe-max-size defaults to 1
 * MB).
 */
static constexpr uint_least64_t MAX_DISCARD = 2ull * 1024ull * 1024ull;

void
DiscardInput(FileDescriptor input, uint_least64_t remaining)
{
	if (remaining > MAX_DISCARD)
		throw SocketProtocolError{"PREMATURE too large"};

	while (remaining > 0) {
		std::array<std::byte, 16384> buffer;
		std::span<std::byte> dest = buffer;
		if (dest.size() > remaining)
			dest = dest.first(remaining);
		ssize_t nbytes = input.Read(dest);
		if (nbytes < 0)
			throw NestException(std::make_exception_ptr(MakeErrno("Read error")),
					    SocketProtocolError{"WAS input pipe error"});
		else if (nbytes == 0)
			throw SocketClosedPrematurelyError{"WAS input pipe closed unexpectedly"};
		else if (static_cast<std::size_t>(nbytes) < dest.size())
			/* we don't accept partial reads because
			   PREMATURE implies that all data is already
			   in the pipe */
			throw SocketClosedPrematurelyError{"Short read on WAS input pipe after PREMATURE"};

		remaining -= nbytes;
	}
}
