/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-parser.h"
#include "uri-escape.h"
#include "uri-verify.h"
#include "strref-pool.h"

#include <string.h>

bool
uri_parse(struct parsed_uri *dest, const char *src)
{
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

    if (!uri_path_verify(dest->base.data, dest->base.length))
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
