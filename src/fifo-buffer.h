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

#ifndef __FIFO_BUFFER_H
#define __FIFO_BUFFER_H

#include "pool.h"

#include <stddef.h>

struct fifo_buffer;

#ifdef __cplusplus
extern "C" {
#endif

struct fifo_buffer *__attr_malloc
fifo_buffer_new(pool_t pool, size_t size);

void
fifo_buffer_clear(struct fifo_buffer *buffer);

const void *
fifo_buffer_read(const struct fifo_buffer *buffer, size_t *length_r);

void
fifo_buffer_consume(struct fifo_buffer *buffer, size_t length);

void *
fifo_buffer_write(struct fifo_buffer *buffer, size_t *max_length_r);

void
fifo_buffer_append(struct fifo_buffer *buffer, size_t length);

int
fifo_buffer_empty(struct fifo_buffer *buffer);

int
fifo_buffer_full(struct fifo_buffer *buffer);

#ifdef __cplusplus
}
#endif


#endif
