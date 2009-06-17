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

int
socket_unix_connect(const char *path)
{
    size_t path_length;
    int fd, ret;
    struct sockaddr_un sa;

    path_length = strlen(path);
    if (path_length >= sizeof(sa.sun_path)) {
        errno = EINVAL;
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    sa.sun_family = AF_UNIX;
    memcpy(sa.sun_path, path, path_length + 1);

    ret = connect(fd, (struct sockaddr*)&sa, sizeof(sa));
    if (ret < 0) {
        int save_errno = errno;
        close(fd);
        errno = save_errno;
        return -1;
    }

    return fd;
}
