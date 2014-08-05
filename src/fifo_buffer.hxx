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

#ifndef FIFO_BUFFER_HXX
#define FIFO_BUFFER_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct fifo_buffer;

/**
 * Initialize a #fifo_buffer object that was allocated by the caller.
 *
 * @param size the total size of the allocation
 */
void
fifo_buffer_init(struct fifo_buffer *buffer, size_t size);

struct fifo_buffer *gcc_malloc
fifo_buffer_new(struct pool *pool, size_t size);

void
fifo_buffer_clear(struct fifo_buffer *buffer);

/**
 * How much space is available for writing?
 */
size_t
fifo_buffer_space(const struct fifo_buffer *buffer);

size_t
fifo_buffer_available(const struct fifo_buffer *buffer);

const void *
fifo_buffer_read(const struct fifo_buffer *buffer, size_t *length_r);

void
fifo_buffer_consume(struct fifo_buffer *buffer, size_t length);

void *
fifo_buffer_write(struct fifo_buffer *buffer, size_t *max_length_r);

void
fifo_buffer_append(struct fifo_buffer *buffer, size_t length);

gcc_pure
int
fifo_buffer_empty(struct fifo_buffer *buffer);

gcc_pure
int
fifo_buffer_full(struct fifo_buffer *buffer);

#endif
