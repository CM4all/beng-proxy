/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_parser.hxx"
#include "uri_verify.hxx"

#include <string.h>

bool
parsed_uri::Parse(const char *src)
{
    const char *qmark = strchr(src, '?');

    const char *semicolon;
    if (qmark == nullptr)
        semicolon = strchr(src, ';');
    else
        semicolon = (const char *)memchr(src, ';', qmark - src);

    base.data = src;
    if (semicolon != nullptr)
        base.length = semicolon - src;
    else if (qmark != nullptr)
        base.length = qmark - src;
    else
        base.length = strlen(src);

    if (!uri_path_verify(base.data, base.length))
        return false;

    if (semicolon == nullptr) {
        strref_clear(&args);
        strref_clear(&path_info);
    } else {
        /* XXX second semicolon for stuff being forwared? */
        args.data = semicolon + 1;
        if (qmark == nullptr)
            args.length = strlen(args.data);
        else
            args.length = qmark - args.data;

        const char *slash = strref_chr(&args, '/');
        if (slash != nullptr) {
            strref_set2(&path_info, slash, strref_end(&args));
            args.length = slash - args.data;
        } else
            strref_clear(&path_info);
    }

    if (qmark == nullptr)
        strref_clear(&query);
    else
        strref_set_c(&query, qmark + 1);

    return true;
}
