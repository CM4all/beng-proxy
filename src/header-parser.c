/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-parser.h"
#include "strutil.h"
#include "strmap.h"
#include "growing-buffer.h"
#include "fifo-buffer.h"
#include "tpool.h"

#include <string.h>

void
header_parse_line(pool_t pool, struct strmap *headers,
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

    strmap_add(headers, key, value);
}

void
header_parse_buffer(pool_t pool, struct strmap *headers,
                    struct growing_buffer *gb)
{
    struct pool_mark mark;
    struct fifo_buffer *buffer;
    void *dest;
    const char *src, *p, *eol;
    size_t max_length, length;

    assert(pool != NULL);
    assert(headers != NULL);
    assert(gb != NULL);

    pool_mark(tpool, &mark);

    buffer = fifo_buffer_new(tpool, 4096);

    while (true) {
        /* copy gb to buffer */

        if (gb != NULL) {
            dest = fifo_buffer_write(buffer, &max_length);
            if (dest != NULL) {
                src = growing_buffer_read(gb, &length);
                if (src != NULL) {
                    if (length > max_length)
                        length = max_length;

                    memcpy(dest, src, length);
                    fifo_buffer_append(buffer, length);
                    growing_buffer_consume(gb, length);
                } else
                    gb = NULL;
            }
        }

        /* parse lines from the buffer */

        p = src = fifo_buffer_read(buffer, &length);
        if (src == NULL && gb == NULL)
            break;

        while (true) {
            while (p < src + length && char_is_whitespace(*p))
                ++p;

            eol = memchr(p, '\n', src + length - p);
            if (eol == NULL) {
                if (gb == NULL)
                    eol = src + length;
                else
                    break;
            }

            while (eol > p && eol[-1] == '\r')
                --eol;

            header_parse_line(pool, headers, p, eol - p);
            p = eol + 1;
        }

        fifo_buffer_consume(buffer, p - src);
    }

    pool_rewind(tpool, &mark);
}
