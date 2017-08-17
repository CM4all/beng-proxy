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

#include "uri_base.hxx"

#include <assert.h>
#include <string.h>

const char *
base_tail(const char *uri, const char *base)
{
    if (base == nullptr)
        return nullptr;

    assert(uri != nullptr);

    const size_t uri_length = strlen(uri);
    const size_t base_length = strlen(base);

    return base_length > 0 && base[base_length - 1] == '/' &&
        uri_length >= base_length && memcmp(uri, base, base_length) == 0
        ? uri + base_length
        : nullptr;
}

const char *
require_base_tail(const char *uri, const char *base)
{
    assert(uri != nullptr);
    assert(base != nullptr);
    assert(memcmp(base, uri, strlen(base)) == 0);

    return uri + strlen(base);
}

size_t
base_string(const char *p, const char *tail)
{
    assert(p != nullptr);
    assert(tail != nullptr);

    size_t length = strlen(p), tail_length = strlen(tail);

    if (length == tail_length)
        /* special case: zero-length prefix (not followed by a
           slash) */
        return memcmp(p, tail, length) == 0
            ? 0 : (size_t)-1;

    return length > tail_length && p[length - tail_length - 1] == '/' &&
        memcmp(p + length - tail_length, tail, tail_length) == 0
        ? length - tail_length
        : (size_t)-1;
}

bool
is_base(const char *uri)
{
    assert(uri != nullptr);

    size_t length = strlen(uri);
    return length > 0 && uri[length - 1] == '/';
}
