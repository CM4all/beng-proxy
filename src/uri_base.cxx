/*
 * Functions for working with base URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_base.hxx"
#include "uri-escape.h"
#include "pool.h"

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
        uri_length > base_length && memcmp(uri, base, base_length) == 0
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

size_t
base_string_unescape(struct pool *pool, const char *p, const char *tail)
{
    assert(pool != nullptr);
    assert(p != nullptr);
    assert(tail != nullptr);

    char *unescaped = p_strdup(pool, tail);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    return base_string(p, unescaped);
}
