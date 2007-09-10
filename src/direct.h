/*
 * Helper inline functions for direct data transfer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DIRECT_H
#define __BENG_DIRECT_H

#ifdef __linux
#include <sys/sendfile.h>

#ifdef SPLICE
#include "splice.h"
#define ISTREAM_DIRECT_SUPPORT (ISTREAM_FILE | ISTREAM_PIPE)
#else
#define ISTREAM_DIRECT_SUPPORT ISTREAM_FILE
#endif


static inline int
istream_direct_to_socket(istream_direct_t src_type, int src_fd,
                         int dest_fd, size_t max_length)
{
#ifdef SPLICE
    if (src_type == ISTREAM_PIPE) {
        return splice(src_fd, NULL, dest_fd, NULL, max_length,
                      SPLICE_F_NONBLOCK | SPLICE_F_MORE | SPLICE_F_MOVE);
    } else {
#endif
        assert(src_type == ISTREAM_FILE);

        return sendfile(dest_fd, src_fd, NULL, max_length);
#ifdef SPLICE
    }
#endif
}

#else /* #ifdef __linux */

#define ISTREAM_DIRECT_SUPPORT 0

#endif  /* #ifdef __linux */

#endif
