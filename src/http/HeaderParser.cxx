/*
 * Copyright 2007-2019 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "HeaderParser.hxx"
#include "pool/pool.hxx"
#include "strmap.hxx"
#include "GrowingBuffer.hxx"
#include "http/HeaderName.hxx"
#include "util/StringView.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StaticFifoBuffer.hxx"
#include "util/StringStrip.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

static constexpr bool
IsValidHeaderValueChar(char ch) noexcept
{
    return ch != '\0' && ch != '\n' && ch != '\r';
}

gcc_pure
static bool
IsValidHeaderValue(StringView value) noexcept
{
    for (char ch : value)
        if (!IsValidHeaderValueChar(ch))
            return false;

    return true;
}

bool
header_parse_line(struct pool &pool, StringMap &headers,
                  StringView line) noexcept
{
    const auto pair = line.Split(':');
    const StringView name = pair.first;
    StringView value = pair.second;

    if (gcc_unlikely(value.IsNull() ||
                     !http_header_name_valid(name) ||
                     !IsValidHeaderValue(value)))
        return false;

    value.StripLeft();

    headers.Add(pool,
                p_strdup_lower(pool, name),
                p_strdup(pool, value));
    return true;
}

void
header_parse_buffer(struct pool &pool, StringMap &headers,
                    GrowingBuffer &&_gb) noexcept
{
    GrowingBufferReader reader(std::move(_gb));

    StaticFifoBuffer<char, 4096> buffer;

    const auto *gb = &_gb;

    while (true) {
        /* copy gb to buffer */

        if (gb != nullptr) {
            auto w = buffer.Write();
            if (!w.empty()) {
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
        if (r.empty() && gb == nullptr)
            break;

        const char *const src = (const char *)r.data;
        const char *p = src;
        const size_t length = r.size;

        while (true) {
            p = StripLeft(p, src + length);

            const char *eol = (const char *)memchr(p, '\n', src + length - p);
            if (eol == nullptr) {
                if (gb == nullptr)
                    eol = src + length;
                else
                    break;
            }

            while (eol > p && eol[-1] == '\r')
                --eol;

            header_parse_line(pool, headers, {p, eol});
            p = eol + 1;
        }

        buffer.Consume(p - src);
    }
}
