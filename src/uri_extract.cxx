/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_extract.hxx"
#include "util/ConstBuffer.hxx"
#include "util/CharUtil.hxx"

#include <assert.h>
#include <string.h>

static constexpr bool
IsValidSchemeStart(char ch)
{
    return IsLowerAlphaASCII(ch);
}

static constexpr bool
IsValidSchemeChar(char ch)
{
    return IsLowerAlphaASCII(ch) || IsDigitASCII(ch) ||
        ch == '+' || ch == '.' || ch == '-';
}

gcc_pure
static bool
IsValidScheme(const char *p, size_t length)
{
    if (length == 0 || !IsValidSchemeStart(*p))
        return false;

    for (size_t i = 1; i < length; ++i)
        if (!IsValidSchemeChar(p[i]))
            return false;

    return true;
}

bool
uri_has_protocol(const char *uri, size_t length)
{
    assert(uri != nullptr);

    const char *colon = (const char *)memchr(uri, ':', length);
    return colon != nullptr &&
        IsValidScheme(uri, colon - uri) &&
        colon < uri + length - 2 &&
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
    if (uri[0] == '/' && uri[1] == '/' && uri[2] != '/')
        return uri + 2;

    if (memcmp(uri, "http://", 7) == 0)
        return uri + 7;

    if (memcmp(uri, "https://", 8) == 0)
        return uri + 8;

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

    const char *ap = uri_after_protocol(uri);
    if (ap != nullptr) {
        const char *p = strchr(ap, '/');
        return p != nullptr
            ? p
            : "";
    }

    return uri;
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
