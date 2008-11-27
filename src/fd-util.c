/*
 * Utilities for file descriptors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fd-util.h"

#include <assert.h>
#include <fcntl.h>
#include <poll.h>

int
fd_mask_descriptor_flags(int fd, int and_mask, int xor_mask)
{
    int ret;

    assert(fd >= 0);

    ret = fcntl(fd, F_GETFD, 0);
    if (ret < 0)
        return ret;

    return fcntl(fd, F_SETFD, (ret & and_mask) ^ xor_mask);
}

int
fd_set_cloexec(int fd)
{
    return fd_mask_descriptor_flags(fd, ~FD_CLOEXEC, FD_CLOEXEC);
}

int
fd_mask_status_flags(int fd, int and_mask, int xor_mask)
{
    int ret;

    assert(fd >= 0);

    ret = fcntl(fd, F_GETFL, 0);
    if (ret < 0)
        return ret;

    return fcntl(fd, F_SETFL, (ret & and_mask) ^ xor_mask);
}

bool
fd_ready_for_writing(int fd)
{
    struct pollfd pollfd = {
        .fd = fd,
        .events = POLLOUT,
    };

    return poll(&pollfd, 1, 0) > 0;
}
