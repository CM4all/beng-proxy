/*
 * Helper inline functions for direct data transfer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DIRECT_H
#define __BENG_DIRECT_H

#include "istream.h"

#include <assert.h>

#ifdef __linux
#include <fcntl.h>
#include <sys/sendfile.h>

#ifdef SPLICE

enum {
    ISTREAM_TO_FILE = ISTREAM_PIPE,
    ISTREAM_TO_SOCKET = ISTREAM_FILE | ISTREAM_PIPE,
    ISTREAM_TO_TCP = ISTREAM_FILE | ISTREAM_PIPE,
};

extern unsigned ISTREAM_TO_PIPE;
extern unsigned ISTREAM_TO_CHARDEV;

void
direct_global_init(void);

void
direct_global_deinit(void);

#else /* !SPLICE */

enum {
    ISTREAM_TO_FILE = 0,
    ISTREAM_TO_PIPE = 0,
    ISTREAM_TO_SOCKET = ISTREAM_FILE,
    ISTREAM_TO_TCP = ISTREAM_FILE,
    ISTREAM_TO_CHARDEV = 0,
}

#endif /* !SPLICE */

static inline ssize_t
istream_direct_to_socket(istream_direct_t src_type, int src_fd,
                         int dest_fd, size_t max_length)
{
#ifdef SPLICE
    if (src_type == ISTREAM_PIPE) {
        return splice(src_fd, NULL, dest_fd, NULL, max_length,
                      SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    } else {
#endif
        assert(src_type == ISTREAM_FILE);

        (void)src_type;

        return sendfile(dest_fd, src_fd, NULL, max_length);
#ifdef SPLICE
    }
#endif
}

#ifdef SPLICE
ssize_t
istream_direct_pipe_to_pipe(int src_fd, int dest_fd, size_t max_length);
#endif

static inline ssize_t
istream_direct_to_pipe(istream_direct_t src_type, int src_fd,
                       int dest_fd, size_t max_length)
{
    (void)src_type;

#ifdef SPLICE
    return splice(src_fd, NULL, dest_fd, NULL, max_length,
                  SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
#else
    return -1;
#endif
}

static inline enum istream_direct
istream_direct_mask_to(enum istream_direct type)
{
    switch (type) {
    case ISTREAM_FILE:
        return ISTREAM_TO_FILE;

    case ISTREAM_PIPE:
        return ISTREAM_TO_PIPE;

    case ISTREAM_SOCKET:
        return ISTREAM_TO_SOCKET;

    case ISTREAM_TCP:
        return ISTREAM_TO_TCP;

    case ISTREAM_CHARDEV:
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

static inline enum istream_direct
istream_direct_mask_to(enum istream_direct type)
{
    (void)type;
    return 0;
}

#endif /* !__linux */

#if !defined(__linux) || !defined(SPLICE)

static inline void
direct_global_init(void) {}

static inline void
direct_global_deinit(void) {}

#endif

/**
 * Determine the minimum number of bytes available on the file
 * descriptor.  Returns -1 if that could not be determined
 * (unsupported fd type or error).
 */
ssize_t
direct_available(int fd, istream_direct_t fd_type, size_t max_length);

#endif
