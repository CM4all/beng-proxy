/*
 * Utilities for file descriptors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fd-util.h"

#include <poll.h>

bool
fd_ready_for_writing(int fd)
{
    struct pollfd pollfd = {
        .fd = fd,
        .events = POLLOUT,
    };

    return poll(&pollfd, 1, 0) > 0;
}
