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
        base.size = semicolon - src;
    else if (qmark != nullptr)
        base.size = qmark - src;
    else
        base.size = strlen(src);

    if (!uri_path_verify(base.data, base.size))
        return false;

    if (semicolon == nullptr) {
        args = nullptr;
        path_info = nullptr;
    } else {
        /* XXX second semicolon for stuff being forwared? */
        args.data = semicolon + 1;
        if (qmark == nullptr)
            args.size = strlen(args.data);
        else
            args.size = qmark - args.data;

        const char *slash = args.Find('/');
        if (slash != nullptr) {
            path_info.data = slash;
            path_info.size = args.end() - slash;
            args.size = slash - args.data;
        } else
            path_info = nullptr;
    }

    if (qmark == nullptr)
        query = nullptr;
    else
        query = { qmark + 1, strlen(qmark + 1) };

    return true;
}
