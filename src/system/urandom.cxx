/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "urandom.hxx"
#include "Error.hxx"
#include "fd_util.h"
#include "util/ScopeExit.hxx"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

static int
Open(const char *path, int flags)
{
    int fd = open_cloexec("/dev/urandom", flags, 0);
    if (fd < 0)
        throw FormatErrno("Failed to open %s", path);

    return fd;
}

static size_t
Read(const char *path, int fd, void *p, size_t size)
{
    ssize_t nbytes = read(fd, p, size);
    if (nbytes < 0)
        throw FormatErrno("Failed to read from %s", path);

    if (nbytes == 0)
        throw std::runtime_error(std::string("Short read from ") + path);

    return nbytes;
}

static void
FullRead(const char *path, int fd, void *_p, size_t size)
{
    uint8_t *p = (uint8_t *)_p;

    while (size > 0) {
        size_t nbytes = Read(path, fd, p, size);
        size -= nbytes;
    }
}

static size_t
Read(const char *path, void *p, size_t size)
{
    int fd = Open(path, O_RDONLY);
    AtScopeExit(fd) { close(fd); };

    return Read(path, fd, p, size);
}

static void
FullRead(const char *path, void *p, size_t size)
{
    int fd = Open(path, O_RDONLY);
    AtScopeExit(fd) { close(fd); };

    FullRead(path, fd, p, size);
}

size_t
UrandomRead(void *p, size_t size)
{
    return Read("/dev/urandom", p, size);
}

void
UrandomFill(void *p, size_t size)
{
    FullRead("/dev/urandom", p, size);
}
