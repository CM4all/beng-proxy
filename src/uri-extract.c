/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-extract.h"
#include "pool.h"

#include <assert.h>
#include <string.h>

bool
uri_has_protocol(const char *uri, size_t length)
{
    assert(uri != NULL);

    const char *colon = memchr(uri, ':', length);
    return colon != NULL && colon < uri + length - 2 &&
        colon[1] == '/' && colon[2] == '/';
}

const char *
uri_host_and_port(struct pool *pool, const char *uri)
{
    assert(pool != NULL);
    assert(uri != NULL);

    if (memcmp(uri, "http://", 7) != 0 && memcmp(uri, "ajp://", 6) != 0)
        return NULL;

    uri += 6 + (uri[0] != 'a');
    const char *slash = strchr(uri, '/');
    if (slash == NULL)
        return uri;

    return p_strndup(pool, uri, slash - uri);
}

const char *
uri_path(const char *uri)
{
    assert(uri != NULL);

    const char *p = strchr(uri, ':');
    if (p == NULL || p[1] != '/')
        return uri;
    if (p[2] != '/')
        return p + 1;
    p = strchr(p + 3, '/');
    if (p == NULL)
        return "";
    return p;
}
