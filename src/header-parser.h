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

void
header_parse_line(pool_t pool, struct strmap *headers,
                  const char *line, size_t length);

#endif
