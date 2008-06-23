/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-relative.h"
#include "strref.h"

#include <string.h>

static bool
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

const struct strref *
uri_relative(const struct strref *base, struct strref *uri)
{
    if (base == NULL || strref_is_empty(base) ||
        uri == NULL || strref_is_empty(uri))
        return NULL;

    if (uri->length >= base->length &&
        memcmp(uri->data, base->data, base->length) == 0) {
        strref_skip(uri, base->length);
        return uri;
    }

    /* special case: http://hostname without trailing slash */
    if (uri->length == base->length - 1 &&
        memcmp(uri->data, base->data, base->length) &&
        memchr(uri->data + 7, '/', uri->length - 7) == NULL) {
        strref_clear(uri);
        return uri;
    }

    return NULL;
}
