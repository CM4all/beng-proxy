// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "DiscardInput.hxx"
#include "system/Error.hxx"
#include "net/SocketProtocolError.hxx"
#include "io/FileDescriptor.hxx"
#include "util/Exception.hxx" // for NestException()

#include <array>

void
DiscardInput(FileDescriptor input, uint_least64_t remaining)
{
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

		remaining -= nbytes;
	}
}
