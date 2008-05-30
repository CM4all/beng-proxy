/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GROWING_BUFFER_H
#define __BENG_GROWING_BUFFER_H

#include "pool.h"
#include "istream.h"

struct growing_buffer *__attr_malloc
growing_buffer_new(pool_t pool, size_t initial_size);

void *
growing_buffer_write(struct growing_buffer *gb, size_t length);

void
growing_buffer_write_buffer(struct growing_buffer *gb, const void *p, size_t length);

void
growing_buffer_write_string(struct growing_buffer *gb, const char *p);

bool
growing_buffer_empty(struct growing_buffer *gb);

const void *
growing_buffer_read(struct growing_buffer *gb, size_t *length_r);

void
growing_buffer_consume(struct growing_buffer *gb, size_t length);

istream_t
growing_buffer_istream(struct growing_buffer *gb);

#endif
