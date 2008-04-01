/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_H
#define __BENG_URI_H

#include "strref.h"
#include "pool.h"

struct parsed_uri {
    struct strref base;
    struct strref args;
    struct strref query;
};

size_t
uri_escape(char *dest, const char *src, size_t src_length);

static inline const char *
uri_escape_dup(pool_t pool, const char *src, size_t src_length)
{
    char *dest = p_malloc(pool, src_length * 3 + 1);
    uri_escape(dest, src, src_length);
    return dest;
}

size_t
uri_unescape_inplace(char *src, size_t length);

int
uri_parse(pool_t pool, struct parsed_uri *dest, const char *src);

const char *
uri_absolute(pool_t pool, const char *base, const char *uri, size_t length);

#endif
