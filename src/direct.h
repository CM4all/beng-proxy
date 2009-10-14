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
    ISTREAM_TO_PIPE = ISTREAM_FILE | ISTREAM_PIPE,
    ISTREAM_TO_SOCKET = ISTREAM_FILE | ISTREAM_PIPE,
};

#else /* !SPLICE */

enum {
    ISTREAM_TO_PIPE = ISTREAM_FILE,
    ISTREAM_TO_SOCKET = ISTREAM_FILE,
}

#endif /* !SPLICE */

static inline int
istream_direct_to_socket(istream_direct_t src_type, int src_fd,
                         int dest_fd, size_t max_length)
{
#ifdef SPLICE
    if (src_type == ISTREAM_PIPE) {
        return splice(src_fd, NULL, dest_fd, NULL, max_length,
                      /* SPLICE_F_NONBLOCK | */ SPLICE_F_MORE | SPLICE_F_MOVE);
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

static inline int
istream_direct_to_pipe(istream_direct_t src_type, int src_fd,
                       int dest_fd, size_t max_length)
{
#ifdef SPLICE
    if (src_type == ISTREAM_PIPE) {
        return istream_direct_pipe_to_pipe(src_fd, dest_fd, max_length);
    } else {
#endif
        (void)src_type;

        return splice(src_fd, NULL, dest_fd, NULL, max_length,
                      /* SPLICE_F_NONBLOCK | */ SPLICE_F_MORE | SPLICE_F_MOVE);
#ifdef SPLICE
    }
#endif
}

#else /* !__linux */

enum {
    ISTREAM_TO_PIPE = 0,
    ISTREAM_TO_SOCKET = 0,
};

#endif /* !__linux */

#endif
