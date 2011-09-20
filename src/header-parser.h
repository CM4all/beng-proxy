/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HEADER_PARSER_H
#define __BENG_HEADER_PARSER_H

#include <stddef.h>

struct pool;
struct strmap;
struct growing_buffer;

void
header_parse_line(struct pool *pool, struct strmap *headers,
                  const char *line, size_t length);

void
header_parse_buffer(struct pool *pool, struct strmap *headers,
                    const struct growing_buffer *gb);

#endif
