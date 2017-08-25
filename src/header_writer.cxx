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

#include "header_writer.hxx"
#include "strmap.hxx"
#include "GrowingBuffer.hxx"
#include "http/HeaderName.hxx"

#include <assert.h>
#include <string.h>

void
header_write_begin(GrowingBuffer &buffer, const char *name)
{
    assert(name != nullptr);
    assert(*name != 0);

    size_t name_length = strlen(name);
    char *dest = (char *)buffer.Write(name_length + 2);

    memcpy(dest, name, name_length);
    dest += name_length;
    *dest++ = ':';
    *dest++ = ' ';
}

void
header_write_finish(GrowingBuffer &buffer)
{
    buffer.Write("\r\n", 2);
}

void
header_write(GrowingBuffer &buffer, const char *key, const char *value)
{
    size_t key_length, value_length;

    assert(key != nullptr);
    assert(value != nullptr);

    key_length = strlen(key);
    value_length = strlen(value);

    char *dest = (char *)buffer.Write(key_length + 2 + value_length + 2);

    memcpy(dest, key, key_length);
    dest += key_length;
    *dest++ = ':';
    *dest++ = ' ';
    memcpy(dest, value, value_length);
    dest += value_length;
    *dest++ = '\r';
    *dest = '\n';
}

void
headers_copy_one(const StringMap &in, GrowingBuffer &out,
                 const char *key)
{
    const char *value = in.Get(key);
    if (value != nullptr)
        header_write(out, key, value);
}

void
headers_copy(const StringMap &in, GrowingBuffer &out,
             const char *const* keys)
{
    for (; *keys != nullptr; ++keys) {
        const char *value = in.Get(*keys);
        if (value != nullptr)
            header_write(out, *keys, value);
    }
}

void
headers_copy_all(const StringMap &in, GrowingBuffer &out)
{
    for (const auto &i : in)
        header_write(out, i.key, i.value);
}

void
headers_copy_most(const StringMap &in, GrowingBuffer &out)
{
    for (const auto &i : in)
        if (!http_header_is_hop_by_hop(i.key))
            header_write(out, i.key, i.value);
}

GrowingBuffer
headers_dup(const StringMap &in)
{
    GrowingBuffer out;
    headers_copy_most(in, out);
    return out;
}
