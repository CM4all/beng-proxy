/*
 * Helper inline functions for direct data transfer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "direct.h"

#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>

#ifdef __linux
#ifdef SPLICE

unsigned ISTREAM_TO_PIPE = ISTREAM_FILE;
unsigned ISTREAM_TO_CHARDEV = 0;

/**
 * Checks whether the kernel supports splice() between the two
 * specified file handle types.
 */
static bool
splice_supported(int src, int dest)
{
    return splice(src, NULL, dest, NULL, 1, SPLICE_F_NONBLOCK) >= 0 ||
        (errno != EINVAL && errno != ENOSYS);
}

void
direct_global_init(void)
{
    int a[2], b[2], fd;

    /* create a pipe and feed some data into it */

    if (pipe(a) < 0)
        abort();

    /* check splice(pipe, pipe) */

    if (pipe(b) < 0)
        abort();

    if (splice_supported(a[0], b[1]))
        ISTREAM_TO_PIPE |= ISTREAM_PIPE;

    close(b[0]);
    close(b[1]);

    /* check splice(pipe, chardev) */

    fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        if (splice_supported(a[0], fd))
            ISTREAM_TO_CHARDEV |= ISTREAM_PIPE;
        close(fd);
    }

    /* check splice(chardev, pipe) */

    fd = open("/dev/zero", O_RDONLY);
    if (fd >= 0) {
        if (splice_supported(fd, a[1]))
            ISTREAM_TO_PIPE |= ISTREAM_CHARDEV;
        close(fd);
    }

    /* check splice(AF_LOCAL, pipe) */
    /* (unsupported in Linux 2.6.31) */

    fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (fd >= 0) {
        if (splice_supported(fd, a[1]))
            ISTREAM_TO_PIPE |= ISTREAM_SOCKET;

        close(fd);
    }

    /* check splice(TCP, pipe) */

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        if (splice_supported(fd, a[1]))
            ISTREAM_TO_PIPE |= ISTREAM_TCP;

        close(fd);
    }

    /* cleanup */

    close(a[0]);
    close(a[1]);
}

void
direct_global_deinit(void)
{
}

#endif /* #ifdefSPLICE */
#endif  /* #ifdef __linux */
