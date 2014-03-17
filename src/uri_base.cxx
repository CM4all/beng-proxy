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
base_string(const char *p, const char *suffix)
{
    assert(p != nullptr);
    assert(suffix != nullptr);

    size_t length = strlen(p), suffix_length = strlen(suffix);

    if (length == suffix_length)
        /* special case: zero-length prefix (not followed by a
           slash) */
        return memcmp(p, suffix, length) == 0
            ? 0 : (size_t)-1;

    return length > suffix_length && p[length - suffix_length - 1] == '/' &&
        memcmp(p + length - suffix_length, suffix, suffix_length) == 0
        ? length - suffix_length
        : (size_t)-1;
}

/**
 * @return (size_t)-1 on mismatch
 */
size_t
base_string_unescape(struct pool *pool, const char *p, const char *suffix)
{
    assert(pool != nullptr);
    assert(p != nullptr);
    assert(suffix != nullptr);

    char *unescaped = p_strdup(pool, suffix);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    return base_string(p, unescaped);
}
