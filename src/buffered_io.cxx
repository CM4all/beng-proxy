/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "buffered_io.hxx"
#include "util/StaticFifoBuffer.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

template<typename B>
ssize_t
read_to_buffer(int fd, B &buffer, size_t length)
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

template
ssize_t
read_to_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer, size_t length);

template
ssize_t
read_to_buffer(int fd, StaticFifoBuffer<uint8_t, 4096> &buffer, size_t length);

template<typename B>
ssize_t
write_from_buffer(int fd, B &buffer)
{
    auto r = buffer.Read();
    if (r.IsEmpty())
        return -2;

    ssize_t nbytes = write(fd, r.data, r.size);
    if (nbytes < 0 && errno != EAGAIN)
        return -1;

    if (nbytes <= 0)
        return r.size;

    buffer.Consume((size_t)nbytes);
    return (ssize_t)r.size - nbytes;
}

template
ssize_t
write_from_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer);

template
ssize_t
write_from_buffer(int fd, StaticFifoBuffer<uint8_t, 4096> &buffer);

template<typename B>
ssize_t
recv_to_buffer(int fd, B &buffer, size_t length)
{
    assert(fd >= 0);

    auto w = buffer.Write();
    if (w.IsEmpty())
        return -2;

    if (length > w.size)
        length = w.size;

    ssize_t nbytes = recv(fd, w.data, length, MSG_DONTWAIT);
    if (nbytes > 0)
        buffer.Append((size_t)nbytes);

    return nbytes;
}

template
ssize_t
recv_to_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer, size_t length);

template
ssize_t
recv_to_buffer(int fd, StaticFifoBuffer<uint8_t, 4096> &buffer, size_t length);

template<typename B>
ssize_t
send_from_buffer(int fd, B &buffer)
{
    auto r = buffer.Read();
    if (r.IsEmpty())
        return -2;

    ssize_t nbytes = send(fd, r.data, r.size, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0 && errno != EAGAIN)
        return -1;

    if (nbytes <= 0)
        return r.size;

    buffer.Consume((size_t)nbytes);
    return (ssize_t)r.size - nbytes;
}

template
ssize_t
send_from_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer);

template
ssize_t
send_from_buffer(int fd, StaticFifoBuffer<uint8_t, 4096> &buffer);
