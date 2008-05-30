/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HEADER_WRITER_H
#define __BENG_HEADER_WRITER_H

#include "pool.h"

struct strmap;
struct growing_buffer;

void
header_write(struct growing_buffer *gb, const char *key, const char *value);

void
headers_copy(struct strmap *in, struct growing_buffer *out,
             const char *const* keys);

void
headers_copy_all(struct strmap *in, struct growing_buffer *out);

struct growing_buffer *
headers_dup(pool_t pool, struct strmap *in);

#endif
