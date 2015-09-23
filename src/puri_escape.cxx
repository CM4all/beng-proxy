/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "puri_escape.hxx"
#include "uri/uri_escape.hxx"
#include "pool.hxx"

const char *
uri_escape_dup(struct pool *pool, const char *src, size_t src_length,
               char escape_char)
{
    char *dest = (char *)p_malloc(pool, src_length * 3 + 1);
    size_t dest_length = uri_escape(dest, src, src_length, escape_char);
    dest[dest_length] = 0;
    return dest;
}

char *
uri_unescape_dup(struct pool *pool, const char *src, size_t length,
                 char escape_char)
{
    char *dest = (char *)p_malloc(pool, length + 1);
    char *end = uri_unescape(dest, src, length, escape_char);
    if (end == nullptr)
        return nullptr;

    *end = 0;
    return dest;
}
