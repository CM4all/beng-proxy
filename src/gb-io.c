/*
 * Utilities for buffered I/O (struct growing_buffer).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "gb-io.h"
#include "growing-buffer.h"

#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

ssize_t
write_from_gb(int fd, struct growing_buffer *gb)
{
    const void *data;
    size_t length;
    ssize_t nbytes;

    data = growing_buffer_read(gb, &length);
    if (data == NULL)
        return -2;

    nbytes = write(fd, data, length);
    if (nbytes < 0 && errno != EAGAIN)
        return -1;

    if (nbytes <= 0)
        return length;

    growing_buffer_consume(gb, (size_t)nbytes);
    return (ssize_t)length - nbytes;
}

ssize_t
send_from_gb(int fd, struct growing_buffer *gb)
{
    const void *data;
    size_t length;
    ssize_t nbytes;

    data = growing_buffer_read(gb, &length);
    if (data == NULL)
        return -2;

    nbytes = send(fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0 && errno != EAGAIN)
        return -1;

    if (nbytes <= 0)
        return length;

    growing_buffer_consume(gb, (size_t)nbytes);
    return (ssize_t)length - nbytes;
}
