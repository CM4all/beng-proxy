/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header_parser.hxx"
#include "strutil.h"
#include "strmap.hxx"
#include "growing_buffer.hxx"
#include "fifo_buffer.hxx"
#include "tpool.h"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

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

    headers->Add(key, value);
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

    GrowingBufferReader reader(*gb);

    struct fifo_buffer *buffer = fifo_buffer_new(tpool, 4096);

    while (true) {
        /* copy gb to buffer */

        if (gb != nullptr) {
            auto w = fifo_buffer_write(buffer);
            if (!w.IsEmpty()) {
                auto src = reader.Read();
                if (!src.IsNull()) {
                    size_t nbytes = std::min(src.size, w.size);
                    memcpy(w.data, src.data, nbytes);
                    fifo_buffer_append(buffer, nbytes);
                    reader.Consume(nbytes);
                } else
                    gb = nullptr;
            }
        }

        /* parse lines from the buffer */

        auto r = fifo_buffer_read(buffer);
        if (r.IsEmpty() && gb == nullptr)
            break;

        const char *const src = (const char *)r.data;
        const char *p = src;
        const size_t length = r.size;

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
