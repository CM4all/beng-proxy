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

#include "args.hxx"
#include "uri/Escape.hxx"
#include "puri_escape.hxx"
#include "strmap.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringView.hxx"

#include <string.h>

static constexpr char ARGS_ESCAPE_CHAR = '$';

StringMap *
args_parse(struct pool *pool, const char *p, size_t length)
{
    const char *end = p + length;
    auto *args = strmap_new(pool);

    do {
        const char *ampersand = (const char *)memchr(p, '&', end - p);
        const char *next = ampersand;
        if (ampersand == nullptr)
            ampersand = end;
        else
            ++next;

        const char *equals = (const char *)memchr(p, '=', ampersand - p);
        if (equals > p) {
            char *value = uri_unescape_dup(*pool, {equals + 1, ampersand},
                                           ARGS_ESCAPE_CHAR);
            if (value != nullptr)
                args->Add(p_strndup(pool, p, equals - p), value);
        }

        p = next;
    } while (p != nullptr);

    return args;
}

const char *
args_format_n(struct pool *pool, const StringMap *args,
              const char *replace_key, StringView replace_value,
              const char *replace_key2, StringView replace_value2,
              const char *replace_key3, StringView replace_value3,
              const char *remove_key)
{
    size_t length = 0;

    /* determine length */

    if (args != nullptr)
        for (const auto &i : *args)
            length += strlen(i.key) + 1 + strlen(i.value) * 3 + 1;

    if (replace_key != nullptr)
        length += strlen(replace_key) + 1 + replace_value.size * 3 + 1;

    if (replace_key2 != nullptr)
        length += strlen(replace_key2) + 1 + replace_value2.size * 3 + 1;

    if (replace_key3 != nullptr)
        length += strlen(replace_key3) + 1 + replace_value3.size * 3 + 1;

    /* allocate memory, format it */

    char *p = (char *)p_malloc(pool, length + 1);
    const char *const ret = p;

    if (args != nullptr) {
        for (const auto &i : *args) {
            if ((replace_key != nullptr && strcmp(i.key, replace_key) == 0) ||
                (replace_key2 != nullptr && strcmp(i.key, replace_key2) == 0) ||
                (replace_key3 != nullptr && strcmp(i.key, replace_key3) == 0) ||
                (remove_key != nullptr && strcmp(i.key, remove_key) == 0))
                continue;
            if (p > ret)
                *p++ = '&';
            length = strlen(i.key);
            memcpy(p, i.key, length);
            p += length;
            *p++ = '=';
            p += uri_escape(p, i.value,
                            ARGS_ESCAPE_CHAR);
        }
    }

    if (replace_key != nullptr) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key);
        memcpy(p, replace_key, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value, ARGS_ESCAPE_CHAR);
    }

    if (replace_key2 != nullptr) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key2);
        memcpy(p, replace_key2, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value2, ARGS_ESCAPE_CHAR);
    }

    if (replace_key3 != nullptr) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key3);
        memcpy(p, replace_key3, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value3, ARGS_ESCAPE_CHAR);
    }

    *p = 0;
    return ret;
}

const char *
args_format(struct pool *pool, const StringMap *args,
            const char *replace_key, const char *replace_value,
            const char *replace_key2, const char *replace_value2,
            const char *remove_key)
{
    return args_format_n(pool, args,
                         replace_key,
                         { replace_value,
                                 replace_value == nullptr ? 0 : strlen(replace_value) },
                         replace_key2,
                         { replace_value2,
                                 replace_value2 == nullptr ? 0 : strlen(replace_value2) },
                         nullptr, nullptr,
                         remove_key);
}
