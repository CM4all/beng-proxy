/*
 * Helper inline functions for direct data transfer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "direct.hxx"

#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#ifdef __linux
#ifdef SPLICE

FdTypeMask ISTREAM_TO_PIPE = FdType::FD_FILE;
FdTypeMask ISTREAM_TO_CHARDEV = 0;

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
direct_global_init()
{
    int a[2], b[2], fd;

    /* create a pipe and feed some data into it */

    if (pipe(a) < 0)
        abort();

    /* check splice(pipe, pipe) */

    if (pipe(b) < 0)
        abort();

    if (splice_supported(a[0], b[1]))
        ISTREAM_TO_PIPE |= FdType::FD_PIPE;

    close(b[0]);
    close(b[1]);

    /* check splice(pipe, chardev) */

    fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        if (splice_supported(a[0], fd))
            ISTREAM_TO_CHARDEV |= FdType::FD_PIPE;
        close(fd);
    }

    /* check splice(chardev, pipe) */

    fd = open("/dev/zero", O_RDONLY);
    if (fd >= 0) {
        if (splice_supported(fd, a[1]))
            ISTREAM_TO_PIPE |= FdType::FD_CHARDEV;
        close(fd);
    }

    /* check splice(AF_LOCAL, pipe) */
    /* (unsupported in Linux 2.6.31) */

    fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (fd >= 0) {
        if (splice_supported(fd, a[1]))
            ISTREAM_TO_PIPE |= FdType::FD_SOCKET;

        close(fd);
    }

    /* check splice(TCP, pipe) */

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        if (splice_supported(fd, a[1]))
            ISTREAM_TO_PIPE |= FdType::FD_TCP;

        close(fd);
    }

    /* cleanup */

    close(a[0]);
    close(a[1]);
}

#endif /* #ifdefSPLICE */
#endif  /* #ifdef __linux */

ssize_t
direct_available(int fd, FdType fd_type, size_t max_length)
{
    if ((fd_type & ISTREAM_TO_CHARDEV) == 0)
        /* unsupported fd type */
        return -1;

    /* XXX this is quite slow, and should be optimized with a
       preallocated pipe */
    int fds[2];
    if (pipe(fds) < 0)
        return -1;

    ssize_t nbytes = tee(fd, fds[1], max_length, SPLICE_F_NONBLOCK);
    if (nbytes < 0) {
        int save_errno = errno;
        close(fds[0]);
        close(fds[1]);
        errno = save_errno;
        return -1;
    }

    close(fds[0]);
    close(fds[1]);

    return nbytes;
}

FdType
guess_fd_type(int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0)
        return FdType::FD_NONE;

    if (S_ISREG(st.st_mode))
        return FdType::FD_FILE;

    if (S_ISCHR(st.st_mode))
        return FdType::FD_CHARDEV;

    if (S_ISFIFO(st.st_mode))
        return FdType::FD_PIPE;

    if (S_ISSOCK(st.st_mode))
        return FdType::FD_SOCKET;

    return FdType::FD_NONE;
}
