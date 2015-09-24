/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "puri_escape.hxx"
#include "uri/uri_escape.hxx"
#include "util/StringView.hxx"
#include "pool.hxx"

const char *
uri_escape_dup(struct pool *pool, StringView src,
               char escape_char)
{
    char *dest = (char *)p_malloc(pool, src.size * 3 + 1);
    size_t dest_length = uri_escape(dest, src, escape_char);
    dest[dest_length] = 0;
    return dest;
}

char *
uri_unescape_dup(struct pool *pool, StringView src,
                 char escape_char)
{
    char *dest = (char *)p_malloc(pool, src.size + 1);
    char *end = uri_unescape(dest, src, escape_char);
    if (end == nullptr)
        return nullptr;

    *end = 0;
    return dest;
}
