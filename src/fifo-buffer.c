/*
 * Copyright (C) 2004-2007 Max Kellermann <max@duempel.org>
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

#include "fifo-buffer.h"

#include <assert.h>
#include <string.h>

struct fifo_buffer {
    size_t size, start, end;
    unsigned char buffer[sizeof(size_t)];
};

struct fifo_buffer *
fifo_buffer_new(pool_t pool, size_t size)
{
    struct fifo_buffer *buffer;

    assert(size > 0);

    buffer = p_malloc(pool, sizeof(*buffer) - sizeof(buffer->buffer) + size);
    buffer->size = size;
    buffer->start = 0;
    buffer->end = 0;

    return buffer;
}

void
fifo_buffer_clear(struct fifo_buffer *buffer)
{
    assert(buffer != NULL);
    buffer->start = 0;
    buffer->end = 0;
}

size_t
fifo_buffer_available(const struct fifo_buffer *buffer)
{
    assert(buffer != NULL);
    assert(buffer->end >= buffer->start);

    return buffer->end - buffer->start;
}

const void *
fifo_buffer_read(const struct fifo_buffer *buffer, size_t *length_r)
{
    assert(buffer != NULL);
    assert(buffer->end >= buffer->start);
    assert(length_r != NULL);

    if (buffer->start == buffer->end)
        return NULL;

    *length_r = buffer->end - buffer->start;
    return buffer->buffer + buffer->start;
}

void
fifo_buffer_consume(struct fifo_buffer *buffer, size_t length)
{
    assert(buffer != NULL);
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

void *
fifo_buffer_write(struct fifo_buffer *buffer, size_t *max_length_r)
{
    assert(buffer != NULL);
    assert(buffer->size > 0);
    assert(buffer->end <= buffer->size);
    assert(max_length_r != NULL);

    if (buffer->end == buffer->size) {
        fifo_buffer_move(buffer);
        if (buffer->end == buffer->size)
            return NULL;
    } else if (buffer->start > 0 && buffer->start == buffer->end) {
        buffer->start = 0;
        buffer->end = 0;
    }

    *max_length_r = buffer->size - buffer->end;
    return buffer->buffer + buffer->end;
}

void
fifo_buffer_append(struct fifo_buffer *buffer, size_t length)
{
    assert(buffer != NULL);
    assert(buffer->size > 0);
    assert(buffer->end >= buffer->start);
    assert(buffer->end + length <= buffer->size);

    buffer->end += length;
}

int
fifo_buffer_empty(struct fifo_buffer *buffer)
{
    assert(buffer != NULL);
    assert(buffer->size > 0);

    return buffer->start == buffer->end;
}

int
fifo_buffer_full(struct fifo_buffer *buffer)
{
    assert(buffer != NULL);
    assert(buffer->size > 0);

    return buffer->start == 0 && buffer->end == buffer->size;

}
