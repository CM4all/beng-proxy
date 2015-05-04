/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header_parser.hxx"
#include "pool.hxx"
#include "strmap.hxx"
#include "growing_buffer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StaticFifoBuffer.hxx"
#include "util/CharUtil.hxx"

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
    while (colon < line + length && IsWhitespaceOrNull(*colon))
        ++colon;

    char *key = p_strndup_lower(pool, line, key_end - line);
    char *value = p_strndup(pool, colon, line + length - colon);

    headers->Add(key, value);
}

void
header_parse_buffer(struct pool *pool, struct strmap *headers,
                    const GrowingBuffer *gb)
{
    assert(pool != nullptr);
    assert(headers != nullptr);
    assert(gb != nullptr);

    GrowingBufferReader reader(*gb);

    StaticFifoBuffer<char, 4096> buffer;

    while (true) {
        /* copy gb to buffer */

        if (gb != nullptr) {
            auto w = buffer.Write();
            if (!w.IsEmpty()) {
                auto src = reader.Read();
                if (!src.IsNull()) {
                    size_t nbytes = std::min(src.size, w.size);
                    memcpy(w.data, src.data, nbytes);
                    buffer.Append(nbytes);
                    reader.Consume(nbytes);
                } else
                    gb = nullptr;
            }
        }

        /* parse lines from the buffer */

        auto r = buffer.Read();
        if (r.IsEmpty() && gb == nullptr)
            break;

        const char *const src = (const char *)r.data;
        const char *p = src;
        const size_t length = r.size;

        while (true) {
            while (p < src + length && IsWhitespaceOrNull(*p))
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

        buffer.Consume(p - src);
    }
}
