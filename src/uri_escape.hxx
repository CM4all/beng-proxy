/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_ESCAPE_HXX
#define BENG_PROXY_URI_ESCAPE_HXX

#include "pool.h"

/**
 * @param escape_char the character that is used to escape; use '%'
 * for normal URIs
 */
size_t
uri_escape(char *dest, const char *src, size_t src_length,
           char escape_char);

gcc_pure gcc_malloc
static inline const char *
uri_escape_dup(struct pool *pool, const char *src, size_t src_length,
               char escape_char)
{
    char *dest = (char *)p_malloc(pool, src_length * 3 + 1);
    size_t dest_length = uri_escape(dest, src, src_length, escape_char);
    dest[dest_length] = 0;
    return dest;
}

/**
 * @param escape_char the character that is used to escape; use '%'
 * for normal URIs
 */
size_t
uri_unescape_inplace(char *src, size_t length, char escape_char);

#endif
