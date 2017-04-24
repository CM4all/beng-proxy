/*
 * Helper inline functions for direct data transfer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DIRECT_HXX
#define BENG_PROXY_DIRECT_HXX

#include "io/FdType.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __linux

#ifdef SPLICE

enum {
    ISTREAM_TO_FILE = FdType::FD_PIPE,
    ISTREAM_TO_SOCKET = FdType::FD_FILE | FdType::FD_PIPE,
    ISTREAM_TO_TCP = FdType::FD_FILE | FdType::FD_PIPE,
};

extern FdTypeMask ISTREAM_TO_PIPE;
extern FdTypeMask ISTREAM_TO_CHARDEV;

#ifdef __cplusplus
extern "C" {
#endif

void
direct_global_init();

#else /* !SPLICE */

enum {
    ISTREAM_TO_FILE = 0,
    ISTREAM_TO_PIPE = 0,
    ISTREAM_TO_SOCKET = FdType::FD_FILE,
    ISTREAM_TO_TCP = FdType::FD_FILE,
    ISTREAM_TO_CHARDEV = 0,
}

#endif /* !SPLICE */

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

#endif /* !__linux */

#if !defined(__linux) || !defined(SPLICE)

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

#ifdef __cplusplus
}
#endif

#endif
