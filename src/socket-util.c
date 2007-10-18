/*
 * Utilities for sockets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "socket-util.h"

#include <assert.h>
#include <sys/socket.h>
#include <fcntl.h>

#ifdef __linux
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

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

#ifdef __linux

int
socket_enable_nodelay(int fd)
{
    int value = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                      &value, sizeof(value));
}

int
socket_set_cork(int fd, int value)
{
    return setsockopt(fd, IPPROTO_TCP, TCP_CORK,
                      &value, sizeof(value));
}

#endif
