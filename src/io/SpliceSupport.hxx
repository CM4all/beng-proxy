// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "io/FdType.hxx"

#include <stddef.h>
#include <sys/types.h>

static constexpr FdTypeMask ISTREAM_TO_FILE = FdType::FD_PIPE;
static constexpr FdTypeMask ISTREAM_TO_SOCKET = FdType::FD_FILE | FdType::FD_PIPE;
static constexpr FdTypeMask ISTREAM_TO_TCP = FdType::FD_FILE | FdType::FD_PIPE;

extern FdTypeMask ISTREAM_TO_PIPE;
extern FdTypeMask ISTREAM_TO_CHARDEV;

void
direct_global_init() noexcept;

[[gnu::const]]
static inline FdTypeMask
istream_direct_mask_to(FdType type) noexcept
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

/**
 * Attempt to guess the type of the file descriptor.  Use only for
 * testing.  In production code, the type shall be passed as a
 * parameter.
 *
 * @return 0 if unknown
 */
[[gnu::pure]]
FdType
guess_fd_type(int fd) noexcept;
