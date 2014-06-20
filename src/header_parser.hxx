/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HEADER_PARSER_HXX
#define BENG_PROXY_HEADER_PARSER_HXX

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
