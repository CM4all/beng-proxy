/*
 * Utilities for sockets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SOCKET_UTIL_H
#define __BENG_SOCKET_UTIL_H

#include <stdbool.h>

int
socket_set_nonblock(int fd, bool value);

int
socket_unix_connect(const char *path);

#endif
