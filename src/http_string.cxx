/*
 * Copyright 2007-2017 Content Management AG
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

#include "http_string.hxx"
#include "pool/pool.hxx"

void
http_next_token(StringView &input, StringView &value)
{
    value.size = 0;
    value.data = input.data;

    while (value.size < input.size &&
           char_is_http_token(input[value.size]))
        ++value.size;

    if (value.size > 0)
        input.skip_front(value.size);
}

void
http_next_quoted_string(struct pool &pool, StringView &input,
                        StringView &value)
{
    char *dest = (char *)p_malloc(&pool, input.size); /* TODO: optimize memory consumption */
    size_t pos = 1;

    value.size = 0;
    value.data = dest;

    while (pos < input.size) {
        if (input[pos] == '\\') {
            ++pos;
            if (pos < input.size)
                dest[value.size++] = input[pos++];
        } else if (input[pos] == '"') {
            ++pos;
            break;
        } else if (char_is_http_text(input[pos])) {
            dest[value.size++] = input[pos++];
        } else {
            ++pos;
        }
    }

    input.skip_front(pos);
}

void
http_next_value(struct pool &pool, StringView &input, StringView &value)
{
    if (!input.empty() && input.front() == '"')
        http_next_quoted_string(pool, input, value);
    else
        http_next_token(input, value);
}

void
http_next_name_value(struct pool &pool, StringView &input,
                     StringView &name, StringView &value)
{
    http_next_token(input, name);
    if (name.empty())
        return;

    input.StripLeft();
    if (!input.empty() && input.front() == '=') {
        input.pop_front();
        input.StripLeft();

        http_next_value(pool, input, value);
    } else
        value = nullptr;
}

size_t
http_quote_string(char *dest, const StringView src)
{
    size_t dest_pos = 0, src_pos = 0;

    dest[dest_pos++] = '"';

    while (src_pos < src.size) {
        if (src[src_pos] == '"' || src[src_pos] == '\\') {
            dest[dest_pos++] = '\\';
            dest[dest_pos++] = src[src_pos++];
        } else if (char_is_http_text(src[src_pos]))
            dest[dest_pos++] = src[src_pos++];
        else
            /* ignore invalid characters */
            ++src_pos;
    }

    dest[dest_pos++] = '"';
    return dest_pos;
}
