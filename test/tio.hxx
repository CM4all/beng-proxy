// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * I/O utilities for unit tests.
 */

#include "system/Error.hxx"

#include <cstddef>
#include <stdexcept>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

static void
read_full(void *_p, std::size_t length)
{
	uint8_t *p = (uint8_t *)_p, *const end = p + length;

	while (p < end) {
		ssize_t nbytes = recv(0, p, length, MSG_WAITALL);
		if (nbytes < 0)
			throw MakeErrno("Failed to receive");
		else if (nbytes == 0)
			throw std::runtime_error{"Socket closed prematurely"};
		p += nbytes;
	}
}

[[maybe_unused]]
static uint8_t
read_byte(std::size_t *remaining_r)
{
	uint8_t value;

	if (*remaining_r < sizeof(value))
		throw std::runtime_error{"Premature end of packet"};

	read_full(&value, sizeof(value));
	(*remaining_r) -= sizeof(value);
	return value;
}

[[maybe_unused]]
static void
discard(std::size_t length)
{
	while (length > 0) {
		uint8_t buffer[1024];
		std::size_t nbytes = length;
		if (nbytes > sizeof(buffer))
			nbytes = sizeof(buffer);
		read_full(buffer, nbytes);
		length -= nbytes;
	}
}

[[maybe_unused]]
static void
write_full(const void *_p, std::size_t length)
{
	const uint8_t *p = (const uint8_t *)_p, *const end = p + length;

	while (p < end) {
		ssize_t nbytes = send(0, p, length, MSG_NOSIGNAL);
		if (nbytes < 0)
			throw MakeErrno("Failed to send");
		else if (nbytes == 0)
			throw std::runtime_error{"Failed to send"};
		p += nbytes;
	}
}
