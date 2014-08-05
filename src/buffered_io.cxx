/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "buffered_io.hxx"
#include "fifo_buffer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

ssize_t
read_to_buffer(int fd, struct fifo_buffer *buffer, size_t length)
{
    assert(fd >= 0);
    assert(buffer != nullptr);

    auto w = fifo_buffer_write(buffer);
    if (w.IsEmpty())
        return -2;

    if (length > w.size)
        length = w.size;

    ssize_t nbytes = read(fd, w.data, length);
    if (nbytes > 0)
        fifo_buffer_append(buffer, (size_t)nbytes);

    return nbytes;
}

ssize_t
write_from_buffer(int fd, struct fifo_buffer *buffer)
{
    auto r = fifo_buffer_read(buffer);
    if (r.IsEmpty())
        return -2;

    ssize_t nbytes = write(fd, r.data, r.size);
    if (nbytes < 0 && errno != EAGAIN)
        return -1;

    if (nbytes <= 0)
        return r.size;

    fifo_buffer_consume(buffer, (size_t)nbytes);
    return (ssize_t)r.size - nbytes;
}

ssize_t
recv_to_buffer(int fd, struct fifo_buffer *buffer, size_t length)
{
    assert(fd >= 0);
    assert(buffer != nullptr);

    auto w = fifo_buffer_write(buffer);
    if (w.IsEmpty())
        return -2;

    if (length > w.size)
        length = w.size;

    ssize_t nbytes = recv(fd, w.data, length, MSG_DONTWAIT);
    if (nbytes > 0)
        fifo_buffer_append(buffer, (size_t)nbytes);

    return nbytes;
}

ssize_t
send_from_buffer(int fd, struct fifo_buffer *buffer)
{
    auto r = fifo_buffer_read(buffer);
    if (r.IsEmpty())
        return -2;

    ssize_t nbytes = send(fd, r.data, r.size, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0 && errno != EAGAIN)
        return -1;

    if (nbytes <= 0)
        return r.size;

    fifo_buffer_consume(buffer, (size_t)nbytes);
    return (ssize_t)r.size - nbytes;
}
