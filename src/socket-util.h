/*
 * Utilities for sockets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SOCKET_UTIL_H
#define __BENG_SOCKET_UTIL_H

int
socket_enable_nonblock(int fd);

#ifdef __linux

int
socket_enable_nodelay(int fd);

int
socket_set_cork(int fd, int value);

#else

int
socket_enable_nodelay(int fd)
{
    (void)fd;
    return 0;
}

int
socket_set_cork(int fd, int value)
{
    (void)fd;
    (void)value;
    return 0;
}

#endif

int
socket_unix_connect(const char *path);

#endif
