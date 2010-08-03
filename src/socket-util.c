/*
 * Utilities for sockets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "socket-util.h"
#include "fd-util.h"

#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#ifdef __linux
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

int
socket_set_nonblock(int fd, bool value)
{
    return fd_mask_status_flags(fd, ~O_NONBLOCK, value ? O_NONBLOCK : 0);
}
