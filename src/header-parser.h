/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HEADER_PARSER_H
#define __BENG_HEADER_PARSER_H

#include "pool.h"

#include <stddef.h>

struct strmap;
struct growing_buffer;

void
header_parse_line(pool_t pool, struct strmap *headers,
                  const char *line, size_t length);

void
header_parse_buffer(pool_t pool, struct strmap *headers,
                    struct growing_buffer *gb);

#endif
