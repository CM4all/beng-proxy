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

#include "ajp_headers.hxx"
#include "ajp_protocol.hxx"
#include "ajp_serialize.hxx"
#include "serialize.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"

#include <assert.h>

static bool
serialize_ajp_header_name(GrowingBuffer &gb, const char *name)
{
    enum ajp_header_code code;

    code = ajp_encode_header_name(name);
    if (code == AJP_HEADER_CONTENT_LENGTH)
        return false;

    if (code == AJP_HEADER_NONE)
        serialize_ajp_string(gb, name);
    else
        serialize_ajp_integer(gb, code);

    return true;
}

unsigned
serialize_ajp_headers(GrowingBuffer &gb, const StringMap &headers)
{
    unsigned n = 0;

    for (const auto &i : headers) {
        if (serialize_ajp_header_name(gb, i.key)) {
            serialize_ajp_string(gb, i.value);
            ++n;
        }
    }

    return n;
}

static void
SkipFront(ConstBuffer<void> &input, size_t n)
{
    assert(input.size >= n);

    input.data = (const uint8_t *)input.data + n;
    input.size -= n;
}

void
deserialize_ajp_headers(struct pool &pool, StringMap &headers,
                        ConstBuffer<void> &input, unsigned num_headers)
{
    while (num_headers-- > 0) {
        unsigned length = deserialize_uint16(input);
        const char *name, *value;
        char *lname;

        if (input.IsNull())
            break;

        if (length >= AJP_HEADER_CODE_START) {
            name = ajp_decode_header_name((enum ajp_header_code)length);
            if (name == nullptr) {
                /* unknown - ignore it, it's the best we can do now */
                deserialize_ajp_string(input);
                continue;
            }
        } else {
            const char *data = (const char *)input.data;
            if (length >= input.size || data[length] != 0)
                /* buffer overflow */
                break;

            name = data;
            SkipFront(input, length + 1);
        }

        value = deserialize_ajp_string(input);
        if (value == nullptr)
            break;

        assert(name != nullptr);

        lname = p_strdup_lower(&pool, name);

        headers.Add(lname, p_strdup(&pool, value));
    }
}

void
deserialize_ajp_response_headers(struct pool &pool, StringMap &headers,
                                 ConstBuffer<void> &input, unsigned num_headers)
{
    while (num_headers-- > 0) {
        unsigned length = deserialize_uint16(input);
        const char *name, *value;
        char *lname;

        if (input.IsNull())
            break;

        if (length >= AJP_RESPONSE_HEADER_CODE_START) {
            name = ajp_decode_response_header_name((enum ajp_response_header_code)length);
            if (name == nullptr) {
                /* unknown - ignore it, it's the best we can do now */
                deserialize_ajp_string(input);
                continue;
            }
        } else {
            const char *data = (const char *)input.data;
            if (length >= input.size || data[length] != 0)
                /* buffer overflow */
                break;

            name = data;
            SkipFront(input, length + 1);
        }

        value = deserialize_ajp_string(input);
        if (value == nullptr)
            break;

        assert(name != nullptr);

        lname = p_strdup_lower(&pool, name);

        headers.Add(lname, p_strdup(&pool, value));
    }
}
