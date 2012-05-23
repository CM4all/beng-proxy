/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-parser.h"
#include "uri-verify.h"

#include <string.h>

bool
uri_parse(struct parsed_uri *dest, const char *src)
{
    const char *qmark = strchr(src, '?');

    const char *semicolon;
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
        strref_clear(&dest->path_info);
    } else {
        /* XXX second semicolon for stuff being forwared? */
        dest->args.data = semicolon + 1;
        if (qmark == NULL)
            dest->args.length = strlen(dest->args.data);
        else
            dest->args.length = qmark - dest->args.data;

        const char *slash = strref_chr(&dest->args, '/');
        if (slash != NULL) {
            strref_set2(&dest->path_info, slash, strref_end(&dest->args));
            dest->args.length = slash - dest->args.data;
        } else
            strref_clear(&dest->path_info);
    }

    if (qmark == NULL)
        strref_clear(&dest->query);
    else
        strref_set_c(&dest->query, qmark + 1);

    return true;
}
