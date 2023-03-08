// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "io/FdType.hxx"

#include <stddef.h>
#include <sys/types.h>

#ifdef __linux

enum {
	ISTREAM_TO_FILE = FdType::FD_PIPE,
	ISTREAM_TO_SOCKET = FdType::FD_FILE | FdType::FD_PIPE,
	ISTREAM_TO_TCP = FdType::FD_FILE | FdType::FD_PIPE,
};

extern FdTypeMask ISTREAM_TO_PIPE;
extern FdTypeMask ISTREAM_TO_CHARDEV;

void
direct_global_init();

[[gnu::const]]
static inline FdTypeMask
istream_direct_mask_to(FdType type)
{
	switch (type) {
	case FdType::FD_NONE:
		return FdType::FD_NONE;

	case FdType::FD_FILE:
		return ISTREAM_TO_FILE;

	case FdType::FD_PIPE:
		return ISTREAM_TO_PIPE;

	case FdType::FD_SOCKET:
		return ISTREAM_TO_SOCKET;

	case FdType::FD_TCP:
		return ISTREAM_TO_TCP;

	case FdType::FD_CHARDEV:
		return ISTREAM_TO_CHARDEV;
	}

	return 0;
}

#else /* !__linux */

enum {
	ISTREAM_TO_PIPE = 0,
	ISTREAM_TO_SOCKET = 0,
	ISTREAM_TO_TCP = 0,
};

static inline FdTypeMask
istream_direct_mask_to([[maybe_unused]] FdType type)
{
	return 0;
}

static inline void
direct_global_init() {}

#endif

/**
 * Determine the minimum number of bytes available on the file
 * descriptor.  Returns -1 if that could not be determined
 * (unsupported fd type or error).
 */
[[gnu::pure]]
ssize_t
direct_available(int fd, FdType fd_type, size_t max_length);

/**
 * Attempt to guess the type of the file descriptor.  Use only for
 * testing.  In production code, the type shall be passed as a
 * parameter.
 *
 * @return 0 if unknown
 */
[[gnu::pure]]
FdType
guess_fd_type(int fd);
