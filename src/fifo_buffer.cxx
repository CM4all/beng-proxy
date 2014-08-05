/*
 * Copyright (C) 2004-2014 Max Kellermann <max@duempel.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of the
 * License.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "fifo_buffer.hxx"
#include "pool.h"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <assert.h>
#include <string.h>

struct fifo_buffer {
    size_t size, start, end;
    unsigned char buffer[sizeof(size_t)];
};

void
fifo_buffer_init(struct fifo_buffer *buffer, size_t size)
{
    assert(buffer != nullptr);
    assert(size >= sizeof(*buffer));

    buffer->size = size + sizeof(buffer->buffer) - sizeof(*buffer);
    buffer->start = 0;
    buffer->end = 0;
}

struct fifo_buffer *
fifo_buffer_new(struct pool *pool, size_t size)
{
    assert(size > 0);

    struct fifo_buffer *buffer = (struct fifo_buffer *)
        p_malloc(pool, sizeof(*buffer) - sizeof(buffer->buffer) + size);
    buffer->size = size;
    buffer->start = 0;
    buffer->end = 0;

    return buffer;
}

void
fifo_buffer_clear(struct fifo_buffer *buffer)
{
    assert(buffer != nullptr);
    buffer->start = 0;
    buffer->end = 0;
}

size_t
fifo_buffer_space(const struct fifo_buffer *buffer)
{
    assert(buffer != nullptr);
    assert(buffer->end >= buffer->start);

    return buffer->start == buffer->end
        ? buffer->size
        : buffer->size - buffer->end;
}

size_t
fifo_buffer_available(const struct fifo_buffer *buffer)
{
    assert(buffer != nullptr);
    assert(buffer->end >= buffer->start);

    return buffer->end - buffer->start;
}

ConstBuffer<void>
fifo_buffer_read(const struct fifo_buffer *buffer)
{
    assert(buffer != nullptr);
    assert(buffer->end >= buffer->start);

    return ConstBuffer<void>(buffer->buffer + buffer->start,
                             buffer->end - buffer->start);
}

void
fifo_buffer_consume(struct fifo_buffer *buffer, size_t length)
{
    assert(buffer != nullptr);
    assert(buffer->end >= buffer->start);
    assert(buffer->start + length <= buffer->end);

    buffer->start += length;
}

static void
fifo_buffer_move(struct fifo_buffer *buffer)
{
    if (buffer->start == 0)
        return;

    if (buffer->end > buffer->start)
        memmove(buffer->buffer,
                buffer->buffer + buffer->start,
                buffer->end - buffer->start);
    buffer->end -= buffer->start;
    buffer->start = 0;
}

WritableBuffer<void>
fifo_buffer_write(struct fifo_buffer *buffer)
{
    assert(buffer != nullptr);
    assert(buffer->size > 0);
    assert(buffer->end <= buffer->size);

    if (buffer->end == buffer->size) {
        fifo_buffer_move(buffer);
    } else if (buffer->start > 0 && buffer->start == buffer->end) {
        buffer->start = 0;
        buffer->end = 0;
    }

    return WritableBuffer<void>(buffer->buffer + buffer->end,
                                buffer->size - buffer->end);
}

void
fifo_buffer_append(struct fifo_buffer *buffer, size_t length)
{
    assert(buffer != nullptr);
    assert(buffer->size > 0);
    assert(buffer->end >= buffer->start);
    assert(buffer->end + length <= buffer->size);

    buffer->end += length;
}

int
fifo_buffer_empty(struct fifo_buffer *buffer)
{
    assert(buffer != nullptr);
    assert(buffer->size > 0);

    return buffer->start == buffer->end;
}

int
fifo_buffer_full(struct fifo_buffer *buffer)
{
    assert(buffer != nullptr);
    assert(buffer->size > 0);

    return buffer->start == 0 && buffer->end == buffer->size;

}
