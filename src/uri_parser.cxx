/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_parser.hxx"
#include "uri-verify.h"

#include <string.h>

bool
uri_parse(struct parsed_uri *dest, const char *src)
{
    const char *qmark = strchr(src, '?');

    const char *semicolon;
    if (qmark == nullptr)
        semicolon = strchr(src, ';');
    else
        semicolon = (const char *)memchr(src, ';', qmark - src);

    dest->base.data = src;
    if (semicolon != nullptr)
        dest->base.length = semicolon - src;
    else if (qmark != nullptr)
        dest->base.length = qmark - src;
    else
        dest->base.length = strlen(src);

    if (!uri_path_verify(dest->base.data, dest->base.length))
        return false;

    if (semicolon == nullptr) {
        strref_clear(&dest->args);
        strref_clear(&dest->path_info);
    } else {
        /* XXX second semicolon for stuff being forwared? */
        dest->args.data = semicolon + 1;
        if (qmark == nullptr)
            dest->args.length = strlen(dest->args.data);
        else
            dest->args.length = qmark - dest->args.data;

        const char *slash = strref_chr(&dest->args, '/');
        if (slash != nullptr) {
            strref_set2(&dest->path_info, slash, strref_end(&dest->args));
            dest->args.length = slash - dest->args.data;
        } else
            strref_clear(&dest->path_info);
    }

    if (qmark == nullptr)
        strref_clear(&dest->query);
    else
        strref_set_c(&dest->query, qmark + 1);

    return true;
}
