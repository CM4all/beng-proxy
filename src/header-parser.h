/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HEADER_PARSER_H
#define __BENG_HEADER_PARSER_H

#include "strmap.h"

#include <stddef.h>

void
header_parse_line(pool_t pool, strmap_t headers,
                  const char *line, size_t length);

#endif
