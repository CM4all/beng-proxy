/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-parser.h"
#include "uri.h"

#include <string.h>

static size_t
uri_path_canonicalize_inplace(char *src, size_t length)
{
    char *end = src + length, *current = src, *p;

    if (src[0] != '/')
        /* path must begin with slash */
        return 0;

    while ((p = memchr(current, '/', end - current - 1)) != NULL) {
        if (p[1] == '/') {
            /* remove double slash */
            memmove(p + 1, p + 2, end - p - 2);
            --end;
            continue;
        }

        if (p[1] == '.') {
            if (p >= end - 2) {
                /* remove trailing "/." */
                end = p + 1;
                break;
            }

            if (p[2] == '/') {
                /* remove "/./" */
                memmove(p + 1, p + 3, end - p - 3);
                end -= 2;
                continue;
            }

            if (p[2] == '.')
                /* no double dot after slash allowed */
                return 0;
        }

        current = p + 1;
    }

    return end - src;
}

/* XXX this is quick and dirty */

int
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
        return -1;

    dest->base.length = uri_path_canonicalize_inplace(p, dest->base.length);
    if (dest->base.length == 0)
        return -1;

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

    return 0;
}
