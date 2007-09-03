/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri.h"
#include "strutil.h"

#include <string.h>

/* XXX this is quick and dirty */

void
uri_parse(struct parsed_uri *dest, const char *src)
{
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
