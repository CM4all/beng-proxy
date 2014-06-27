/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header_parser.hxx"
#include "strutil.h"
#include "strmap.h"
#include "growing_buffer.hxx"
#include "fifo-buffer.h"
#include "tpool.h"
#include "util/ConstBuffer.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

void
header_parse_line(struct pool *pool, struct strmap *headers,
                  const char *line, size_t length)
{
    const char *colon = (const char *)memchr(line, ':', length);
    if (unlikely(colon == nullptr || colon == line))
        return;

    const char *key_end = colon;

    ++colon;
    if (likely(colon < line + length && *colon == ' '))
        ++colon;
    while (colon < line + length && char_is_whitespace(*colon))
        ++colon;

    char *key = p_strndup(pool, line, key_end - line);
    char *value = p_strndup(pool, colon, line + length - colon);

    str_to_lower(key);

    strmap_add(headers, key, value);
}

void
header_parse_buffer(struct pool *pool, struct strmap *headers,
                    const struct growing_buffer *gb)
{
    assert(pool != nullptr);
    assert(headers != nullptr);
    assert(gb != nullptr);

    struct pool_mark_state mark;
    pool_mark(tpool, &mark);

    struct growing_buffer_reader reader(*gb);

    struct fifo_buffer *buffer = fifo_buffer_new(tpool, 4096);

    while (true) {
        /* copy gb to buffer */

        if (gb != nullptr) {
            size_t max_length;
            void *dest = fifo_buffer_write(buffer, &max_length);
            if (dest != nullptr) {
                auto src = reader.Read();
                if (!src.IsNull()) {
                    size_t nbytes = std::min(src.size, max_length);
                    memcpy(dest, src.data, nbytes);
                    fifo_buffer_append(buffer, nbytes);
                    reader.Consume(nbytes);
                } else
                    gb = nullptr;
            }
        }

        /* parse lines from the buffer */

        const char *src, *p;
        size_t length;
        p = src = (const char *)fifo_buffer_read(buffer, &length);
        if (src == nullptr && gb == nullptr)
            break;

        while (true) {
            while (p < src + length && char_is_whitespace(*p))
                ++p;

            const char *eol = (const char *)memchr(p, '\n', src + length - p);
            if (eol == nullptr) {
                if (gb == nullptr)
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
