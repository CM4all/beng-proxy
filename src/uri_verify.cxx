/*
 * Verify URI parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_verify.hxx"
#include "uri_chars.hxx"

#include <string.h>

bool
uri_segment_verify(const char *src, const char *end)
{
    for (; src < end; ++src) {
        /* XXX check for invalid escaped characters? */

        if (!char_is_uri_pchar(*src))
            return false;
    }

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
        slash = (const char *)memchr(src, '/', end - src);
        if (slash == nullptr)
            slash = end;

        if (!uri_segment_verify(src, slash))
            return false;

        src = slash + 1;
    }

    return true;
}

static constexpr bool
IsEncodedNul(const char *p)
{
    return p[0] == '%' && p[1] == '0' && p[2] == '0';
}

static bool
IsEncodedDot(const char *p)
{
    return p[0] == '%' && p[1] == '2' &&
        (p[2] == 'e' || p[2] == 'E');
}

static constexpr bool
IsEncodedSlash(const char *p)
{
    return p[0] == '%' && p[1] == '2' &&
        (p[2] == 'f' || p[2] == 'F');
}

bool
uri_path_verify_paranoid(const char *uri)
{
    if (uri[0] == '.' &&
        (uri[1] == 0 || uri[1] == '/' ||
         (uri[1] == '.' && (uri[2] == 0 || uri[2] == '/')) ||
         IsEncodedDot(uri + 1)))
        /* no ".", "..", "./", "../" */
        return false;

    if (IsEncodedDot(uri))
        return false;

    while (*uri != 0 && *uri != '?') {
        if (*uri == '%') {
            if (/* don't allow an encoded NUL character */
                IsEncodedNul(uri) ||
                /* don't allow an encoded slash (somebody trying to
                   hide a hack?) */
                IsEncodedSlash(uri))
                return false;

            ++uri;
        } else if (*uri == '/') {
            ++uri;

            if (IsEncodedDot(uri))
                /* encoded dot after a slash - what's this client
                   trying to hide? */
                return false;

            if (*uri == '.') {
                ++uri;

                if (IsEncodedDot(uri))
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
uri_path_verify_quick(const char *uri)
{
    if (*uri != '/')
        /* must begin with a slash */
        return false;

    for (++uri; *uri != 0; ++uri)
        if ((signed char)*uri <= 0x20)
            return false;

    return uri;
}
