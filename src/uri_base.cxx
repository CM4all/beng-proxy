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

/**
 * Determine the length of the base prefix in the given string.
 *
 * @return (size_t)-1 on mismatch
 */
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

/**
 * @return (size_t)-1 on mismatch
 */
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
