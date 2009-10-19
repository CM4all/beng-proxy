/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-parser.h"
#include "uri-escape.h"
#include "uri-string.h"
#include "strref-pool.h"

#include <string.h>

static bool
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

static bool
uri_base_verify(const char *src, size_t length)
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

bool
uri_parse(pool_t pool, struct parsed_uri *dest, const char *src)
{
    char *p;
    const char *semicolon, *qmark;

    qmark = strchr(src, '?');

    if (qmark == NULL)
        semicolon = strchr(src, ';');
    else
        semicolon = memchr(src, ';', qmark - src);

    dest->base.data = src;
    if (semicolon != NULL)
        dest->base.length = semicolon - src;
    else if (qmark != NULL)
        dest->base.length = qmark - src;
    else
        dest->base.length = strlen(src);

    dest->base.data = p = strref_dup(pool, &dest->base);
    dest->base.length = uri_unescape_inplace(p, dest->base.length);
    if (dest->base.length == 0)
        return false;

    if (!uri_base_verify(dest->base.data, dest->base.length))
        return false;

    if (semicolon == NULL) {
        strref_clear(&dest->args);
    } else {
        /* XXX second semicolon for stuff being forwared? */
        dest->args.data = semicolon + 1;
        if (qmark == NULL)
            dest->args.length = strlen(dest->args.data);
        else
            dest->args.length = qmark - dest->args.data;
    }

    if (qmark == NULL)
        strref_clear(&dest->query);
    else
        strref_set_c(&dest->query, qmark + 1);

    return true;
}
