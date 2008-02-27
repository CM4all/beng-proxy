/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GROWING_BUFFER_H
#define __BENG_GROWING_BUFFER_H

#include "pool.h"
#include "istream.h"

typedef struct growing_buffer *growing_buffer_t;

growing_buffer_t __attr_malloc
growing_buffer_new(pool_t pool, size_t initial_size);

void *
growing_buffer_write(growing_buffer_t gb, size_t length);

void
growing_buffer_write_buffer(growing_buffer_t gb, const void *p, size_t length);

void
growing_buffer_write_string(growing_buffer_t gb, const char *p);

const void *
growing_buffer_read(growing_buffer_t gb, size_t *length_r);

void
growing_buffer_consume(growing_buffer_t gb, size_t length);

istream_t
growing_buffer_istream(growing_buffer_t gb);

#endif
