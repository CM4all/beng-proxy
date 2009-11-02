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

    if (length == 0 || src[0] != '/')
        /* path must begin with slash */
        return false;

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

static bool
is_encoded_dot(const char *p)
{
    return p[0] == '%' && p[1] == '2' &&
        (p[2] == 'e' || p[2] == 'E');
}

bool
uri_path_verify_paranoid(const char *uri)
{
    if (uri[0] == '.' &&
        (uri[1] == 0 || uri[1] == '/' ||
         (uri[1] == '.' && (uri[2] == 0 || uri[2] == '/')) ||
         is_encoded_dot(uri + 1)))
        /* no ".", "..", "./", "../" */
        return false;

    if (is_encoded_dot(uri))
        return false;

    while (*uri != 0) {
        if (*uri == '%') {
            ++uri;

            if (uri[0] == '0' && uri[1] == '0')
                /* don't allow an encoded NUL character */
                return false;

            if (uri[0] == '2' && (uri[1] == 'f' || uri[1] == 'F'))
                /* don't allow an encoded slash (somebody trying to
                   hide a hack?) */
                return false;
        } else if (*uri == '/') {
            ++uri;

            if (*uri == '/')
                return false;

            if (is_encoded_dot(uri))
                /* encoded dot after a slash - what's this client
                   trying to hide? */
                return false;

            if (*uri == '.') {
                ++uri;

                if (is_encoded_dot(uri))
                    /* encoded dot after a real dot - smells fishy */
                    return false;

                if (*uri == 0 || *uri == '/')
                    return false;

                if (*uri == '.')
                    /* disallow two dots after a slash, even if
                       something else follows - this is the paranoid
                       function after all! */
                    return false;
            }
        } else
            ++uri;
    }

    return true;
}

bool
uri_verify_quick(const char *uri)
{
    if (*uri != '/')
        /* must begin with a slash */
        return false;

    for (++uri; *uri != 0; ++uri)
        if ((signed char)*uri <= 0x20)
            return false;

    return uri;
}
