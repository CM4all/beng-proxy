/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "urandom.hxx"
#include "system/Error.hxx"
#include "io/UniqueFileDescriptor.hxx"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

static UniqueFileDescriptor
Open(const char *path)
{
    UniqueFileDescriptor fd;
    if (!fd.OpenReadOnly(path))
        throw FormatErrno("Failed to open %s", path);

    return fd;
}

static size_t
Read(const char *path, FileDescriptor fd, void *p, size_t size)
{
    ssize_t nbytes = fd.Read(p, size);
    if (nbytes < 0)
        throw FormatErrno("Failed to read from %s", path);

    if (nbytes == 0)
        throw std::runtime_error(std::string("Short read from ") + path);

    return nbytes;
}

static void
FullRead(const char *path, FileDescriptor fd, void *_p, size_t size)
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
    return Read(path, Open(path).ToFileDescriptor(), p, size);
}

static void
FullRead(const char *path, void *p, size_t size)
{
    FullRead(path, Open(path).ToFileDescriptor(), p, size);
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
