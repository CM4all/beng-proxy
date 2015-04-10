/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_extract.hxx"
#include "pool.hxx"

#include <assert.h>
#include <string.h>

bool
uri_has_protocol(const char *uri, size_t length)
{
    assert(uri != nullptr);

    const char *colon = (const char *)memchr(uri, ':', length);
    return colon != nullptr && colon < uri + length - 2 &&
        colon[1] == '/' && colon[2] == '/';
}

const char *
uri_host_and_port(struct pool *pool, const char *uri)
{
    assert(pool != nullptr);
    assert(uri != nullptr);

    if (memcmp(uri, "http://", 7) != 0 && memcmp(uri, "ajp://", 6) != 0)
        return nullptr;

    uri += 6 + (uri[0] != 'a');
    const char *slash = strchr(uri, '/');
    if (slash == nullptr)
        return uri;

    return p_strndup(pool, uri, slash - uri);
}

const char *
uri_path(const char *uri)
{
    assert(uri != nullptr);

    const char *p = strchr(uri, ':');
    if (p == nullptr || p[1] != '/')
        return uri;
    if (p[2] != '/')
        return p + 1;
    p = strchr(p + 3, '/');
    if (p == nullptr)
        return "";
    return p;
}

const char *
uri_query_string(const char *uri)
{
    assert(uri != nullptr);

    const char *p = strchr(uri, '?');
    if (p == nullptr || *++p == 0)
        return nullptr;

    return p;
}
