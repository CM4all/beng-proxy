/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-parser.h"
#include "strutil.h"

#include <string.h>

void
header_parse_line(pool_t pool, strmap_t headers,
                  const char *line, size_t length)
{
    const char *colon, *key_end;
    char *key, *value;

    colon = memchr(line, ':', length);
    if (unlikely(colon == NULL || colon == line))
        return;

    key_end = colon;

    ++colon;
    if (likely(*colon == ' '))
        ++colon;
    while (colon < line + length && char_is_whitespace(*colon))
        ++colon;

    key = p_strndup(pool, line, key_end - line);
    value = p_strndup(pool, colon, line + length - colon);

    str_to_lower(key);

    strmap_addn(headers, key, value);
}
