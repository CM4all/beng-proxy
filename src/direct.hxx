/*
 * Helper inline functions for direct data transfer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DIRECT_HXX
#define BENG_PROXY_DIRECT_HXX

#include "FdType.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <stddef.h>

#ifdef __linux
#include <fcntl.h>
#include <sys/sendfile.h>

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

static inline ssize_t
istream_direct_to_socket(FdType src_type, int src_fd,
                         int dest_fd, size_t max_length)
{
    assert(src_fd != dest_fd);
#ifdef SPLICE
    if (src_type == FdType::FD_PIPE) {
        return splice(src_fd, NULL, dest_fd, NULL, max_length,
                      SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    } else {
#endif
        assert(src_type == FdType::FD_FILE);

        (void)src_type;

        return sendfile(dest_fd, src_fd, NULL, max_length);
#ifdef SPLICE
    }
#endif
}

static inline ssize_t
istream_direct_to_pipe(FdType src_type, int src_fd,
                       int dest_fd, size_t max_length)
{
    (void)src_type;

    assert(src_fd != dest_fd);

#ifdef SPLICE
    return splice(src_fd, NULL, dest_fd, NULL, max_length,
                  SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
#else
    return -1;
#endif
}

static inline ssize_t
istream_direct_to(int src_fd, FdType src_type,
                  int dest_fd, FdType dest_type,
                  size_t max_length)
{
    return IsAnySocket(dest_type)
        ? istream_direct_to_socket(src_type, src_fd, dest_fd, max_length)
        : istream_direct_to_pipe(src_type, src_fd, dest_fd, max_length);
}

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
