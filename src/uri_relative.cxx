/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_relative.hxx"
#include "uri_extract.hxx"
#include "strref.h"
#include "pool.hxx"

#include <string.h>

const char *
uri_compress(struct pool *pool, const char *uri)
{
    assert(pool != nullptr);
    assert(uri != nullptr);

    while (uri[0] == '.' && uri[1] == '/')
        uri += 2;

    if (uri[0] == '.' && uri[1] == '.' &&
        (uri[2] == '/' || uri[2] == 0))
        return nullptr;

    if (strstr(uri, "//") == nullptr &&
        strstr(uri, "/./") == nullptr &&
        strstr(uri, "/..") == nullptr)
        /* cheap route: the URI is already compressed, do not
           duplicate anything */
        return uri;

    char *dest = p_strdup(pool, uri);

    /* eliminate "//" */

    char *p;
    while ((p = strstr(dest, "//")) != nullptr)
        /* strcpy() might be better here, but it does not allow
           overlapped arguments */
        memmove(p + 1, p + 2, strlen(p + 2) + 1);

    /* eliminate "/./" */

    while ((p = strstr(dest, "/./")) != nullptr)
        /* strcpy() might be better here, but it does not allow
           overlapped arguments */
        memmove(p + 1, p + 3, strlen(p + 3) + 1);

    /* eliminate "/../" with backtracking */

    while ((p = strstr(dest, "/../")) != nullptr) {
        if (p == dest) {
            /* this ".." cannot be resolved - scream! */
            p_free(pool, dest);
            return nullptr;
        }

        char *q = p;

        /* backtrack to the previous slash - we can't use strrchr()
           here, and memrchr() is not portable :( */

        do {
            --q;
        } while (q >= dest && *q != '/');

        /* kill it */

        memmove(q + 1, p + 4, strlen(p + 4) + 1);
    }

    /* eliminate trailing "/." and "/.." */

    p = strrchr(dest, '/');
    if (p != nullptr) {
        if (p[1] == '.' && p[2] == 0)
            p[1] = 0;
        else if (p[1] == '.' && p[2] == '.' && p[3] == 0) {
            if (p == dest) {
                /* refuse to delete the leading slash */
                p_free(pool, dest);
                return nullptr;
            }

            *p = 0;

            p = strrchr(dest, '/');
            if (p == nullptr) {
                /* if the string doesn't start with a slash, then an
                   empty return value is allowed */
                p_free(pool, dest);
                return "";
            }

            p[1] = 0;
        }
    }

    if (dest[0] == '.' && dest[1] == 0) {
        /* if the string doesn't start with a slash, then an empty
           return value is allowed */
        p_free(pool, dest);
        return "";
    }

    return dest;
}

static const char *
uri_after_last_slash(const char *uri)
{
    const char *path = uri_path(uri);
    if (path == nullptr)
        return nullptr;

    uri = strrchr(path, '/');
    if (uri != nullptr)
        ++uri;
    return uri;
}

const char *
uri_absolute(struct pool *pool, const char *base, const char *uri, size_t length)
{
    assert(base != nullptr);
    assert(uri != nullptr || length == 0);

    if (length == 0)
        return base;

    if (uri_has_protocol(uri, length))
        return p_strndup(pool, uri, length);

    size_t base_length;
    if (uri[0] == '/' && uri[1] == '/') {
        const char *colon = strstr(base, "://");
        if (colon != nullptr)
            base_length = colon + 1 - base;
        else {
            /* fallback, not much else we can do */
            base = "http:";
            base_length = 5;
        }
    } else if (uri[0] == '/') {
        if (base[0] == '/')
            return p_strndup(pool, uri, length);

        const char *base_path = uri_path(base);
        if (base_path == nullptr)
            return p_strncat(pool, base, strlen(base), "/", 1,
                             uri, length, nullptr);

        base_length = base_path - base;
    } else if (uri[0] == '?') {
        const char *qmark = strchr(base, '?');
        base_length = qmark != nullptr ? (size_t)(qmark - base) : strlen(base);
    } else {
        const char *base_end = uri_after_last_slash(base);
        if (base_end == nullptr)
            return p_strncat(pool, base, strlen(base), "/", 1,
                             uri, length, nullptr);

        base_length = base_end - base;
    }

    char *dest = PoolAlloc<char>(*pool, base_length + length + 1);
    memcpy(dest, base, base_length);
    memcpy(dest + base_length, uri, length);
    dest[base_length + length] = 0;
    return dest;
}

const struct strref *
uri_relative(const struct strref *base, struct strref *uri)
{
    if (base == nullptr || strref_is_empty(base) ||
        uri == nullptr || strref_is_empty(uri))
        return nullptr;

    if (uri->length >= base->length &&
        memcmp(uri->data, base->data, base->length) == 0) {
        strref_skip(uri, base->length);
        return uri;
    }

    /* special case: http://hostname without trailing slash */
    if (uri->length == base->length - 1 &&
        memcmp(uri->data, base->data, base->length) &&
        memchr(uri->data + 7, '/', uri->length - 7) == nullptr) {
        strref_clear(uri);
        return uri;
    }

    return nullptr;
}
