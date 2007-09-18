/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri.h"
#include "strutil.h"

#include <string.h>

static int
parse_hexdigit(char ch)
{
    if (char_is_digit(ch))
        return ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 0x10;
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 0x10;
    else
        return -1;
}

static size_t
uri_unescape_inplace(char *src, size_t length)
{
    char *end = src + length, *current = src, *p;
    int digit1, digit2;
    char ch;

    while ((p = memchr(current, '%', end - current)) != NULL) {
        if (p >= end - 2)
            /* percent sign at the end of string */
            return 0;

        digit1 = parse_hexdigit(p[1]);
        digit2 = parse_hexdigit(p[2]);
        if (digit1 == -1 || digit2 == -1)
            /* invalid hex digits */
            return 0;

        ch = (char)((digit1 << 4) | digit2);
        if (ch == 0)
            /* no %00 hack allowed! */
            return 0;

        *p = ch;
        memmove(p + 1, p + 3, end - p - 3);
        end -= 2;
    }

    return end - src;
}

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

    dest->base = src;
    if (semicolon != NULL)
        dest->base_length = semicolon - src;
    else if (qmark != NULL)
        dest->base_length = qmark - src;
    else
        dest->base_length = strlen(src);

    dest->base = p = p_strndup(pool, dest->base, dest->base_length);
    dest->base_length = uri_unescape_inplace(p, dest->base_length);
    if (dest->base_length == 0)
        return -1;

    dest->base_length = uri_path_canonicalize_inplace(p, dest->base_length);
    if (dest->base_length == 0)
        return -1;

    if (semicolon == NULL) {
        dest->args = NULL;
        dest->args_length = 0;
    } else {
        /* XXX second semicolon for stuff being forwared? */
        dest->args = semicolon + 1;
        if (qmark == NULL)
            dest->args_length = strlen(dest->args);
        else
            dest->args_length = qmark - dest->args;
    }

    if (qmark == NULL) {
        dest->query = NULL;
        dest->query_length = 0;
    } else {
        dest->query = qmark + 1;
        dest->query_length = strlen(dest->query);
    }

    return 0;
}


static int
uri_has_protocol(const char *uri, size_t length)
{
    const char *colon = memchr(uri, ':', length);
    return colon != NULL && colon < uri + length - 2 &&
        colon[1] == '/' && colon[2] == '/';
}

static const char *
uri_path(const char *uri)
{
    if (uri[0] == '/')
        return uri;
    uri = strchr(uri, ':');
    if (uri == NULL)
        return uri;
    ++uri;
    while (*uri == '/')
        ++uri;
    return strchr(uri, '/');
}

static const char *
uri_after_last_slash(const char *uri)
{
    uri = strrchr(uri, '/');
    if (uri != NULL)
        ++uri;
    return uri;
}

const char *
uri_absolute(pool_t pool, const char *base, const char *uri, size_t length)
{
    size_t base_length;
    char *dest;

    if (base == NULL || length == 0)
        return base;

    if (uri_has_protocol(uri, length))
        return NULL;

    if (uri[0] == '/') {
        const char *base_path;

        if (base[0] == '/')
            return NULL;

        base_path = uri_path(base);
        if (base_path == NULL)
            return NULL;

        base_length = base_path - base;
    } else {
        const char *base_end = uri_after_last_slash(base);
        if (base_end == NULL)
            return NULL;

        base_length = base_end - base;
    }

    dest = p_malloc(pool, base_length + length + 1);
    memcpy(dest, base, base_length);
    memcpy(dest + base_length, uri, length);
    dest[base_length + length] = 0;
    return dest;
}
