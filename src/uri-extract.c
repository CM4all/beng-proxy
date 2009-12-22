/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-extract.h"

#include <string.h>

const char *
uri_host_and_port(pool_t pool, const char *uri)
{
    const char *slash;

    if (memcmp(uri, "http://", 7) != 0)
        return NULL;

    uri += 7;
    slash = strchr(uri, '/');
    if (slash == NULL)
        return uri;

    return p_strndup(pool, uri, slash - uri);
}

const char *
uri_path(const char *uri)
{
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
