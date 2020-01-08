/*
 * Copyright 2007-2017 Content Management AG
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

/*
 * Helper inline functions for direct data transfer.
 */

#ifndef BENG_PROXY_DIRECT_HXX
#define BENG_PROXY_DIRECT_HXX

#include "io/FdType.hxx"

#include "util/Compiler.h"

#include <assert.h>
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

gcc_const
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
istream_direct_mask_to(gcc_unused FdType type)
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
gcc_pure
ssize_t
direct_available(int fd, FdType fd_type, size_t max_length);

/**
 * Attempt to guess the type of the file descriptor.  Use only for
 * testing.  In production code, the type shall be passed as a
 * parameter.
 *
 * @return 0 if unknown
 */
gcc_pure
FdType
guess_fd_type(int fd);

#endif
