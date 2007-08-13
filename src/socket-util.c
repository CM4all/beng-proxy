/*
 * Utilities for sockets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "socket-util.h"

#include <assert.h>
#include <sys/socket.h>
#include <fcntl.h>

int
socket_set_nonblock(int fd)
{
    int ret;

    assert(fd >= 0);

    ret = fcntl(fd, F_GETFL, 0);
    if (ret < 0)
        return ret;

    return fcntl(fd, F_SETFL, ret | O_NONBLOCK);
}
