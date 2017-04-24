/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Buffered.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <assert.h>
#include <unistd.h>
#include <errno.h>

ssize_t
read_to_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer, size_t length)
{
    assert(fd >= 0);

    auto w = buffer.Write();
    if (w.IsEmpty())
        return -2;

    if (length > w.size)
        length = w.size;

    ssize_t nbytes = read(fd, w.data, length);
    if (nbytes > 0)
        buffer.Append((size_t)nbytes);

    return nbytes;
}

ssize_t
write_from_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer)
{
    auto r = buffer.Read();
    if (r.IsEmpty())
        return -2;

    ssize_t nbytes = write(fd, r.data, r.size);
    if (nbytes >= 0)
        buffer.Consume((size_t)nbytes);
    else if (errno == EAGAIN)
        nbytes = 0;

    return nbytes;
}
