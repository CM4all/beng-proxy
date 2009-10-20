/*
 * Verify URI parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-verify.h"
#include "uri-string.h"

#include <string.h>

bool
uri_segment_verify(const char *src, const char *end)
{
    if (src == end)
        /* double slash not allowed, see RFC 2396 3.3: "The path may
           consist of a sequence of path segments separated by a
           single slash "/" character." */
        return false;

    do {
        /* XXX check for invalid escaped characters? */

        if (!char_is_uri_pchar(*src))
            return false;
    } while (++src < end);

    return true;
}

bool
uri_path_verify(const char *src, size_t length)
{
    const char *end = src + length, *slash;

    if (src[0] != '/')
        /* path must begin with slash */
        return 0;

    ++src;
    while (src < end) {
        slash = memchr(src, '/', end - src);
        if (slash == NULL)
            slash = end;

        if (!uri_segment_verify(src, slash))
            return false;

        src = slash + 1;
    }

    return true;
}
