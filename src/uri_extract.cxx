/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_extract.hxx"
#include "util/ConstBuffer.hxx"

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

/**
 * Return the URI part after the protocol specification (and after the
 * double slash).
 */
gcc_pure
static const char *
uri_after_protocol(const char *uri)
{
    if (memcmp(uri, "http://", 7) == 0)
        return uri + 7;

    if (memcmp(uri, "ajp://", 6) == 0)
        return uri + 6;

    return nullptr;
}

ConstBuffer<char>
uri_host_and_port(const char *uri)
{
    assert(uri != nullptr);

    uri = uri_after_protocol(uri);
    if (uri == nullptr)
        return nullptr;

    const char *slash = strchr(uri, '/');
    if (slash == nullptr)
        return { uri, strlen(uri) };

    return { uri, size_t(slash - uri) };
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
