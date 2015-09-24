/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "relocate_uri.hxx"
#include "http_address.hxx"
#include "pool.hxx"
#include "uri/uri_extract.hxx"
#include "util/StringView.hxx"

/**
 * If the given URI matches the #HttpAddress regarding and port, then
 * return the URI path.  If not, return nullptr.
 */
gcc_pure
static const char *
MatchUriHost(const char *uri, const char *host)
{
    const auto &h = uri_host_and_port(uri);
    if (!h.IsNull()) {
        if (host == nullptr)
            /* this is URI_SCHEME_UNIX, and its host cannot be
               verified */
            return nullptr;

        if (memcmp(h.data, host, h.size) != 0 || host[h.size] != 0)
            /* host/port mismatch */
            return nullptr;

        uri = h.end();
    }

    if (*uri != '/')
        /* relative URIs are not (yet?) supported here */
        return nullptr;

    return uri;
}

gcc_pure
static StringView
UriBaseTail(StringView uri, StringView base)
{
    return uri.StartsWith(base)
        ? StringView(uri.data + base.size, uri.end())
        : nullptr;
}

gcc_pure
static StringView
UriPrefixBeforeTail(StringView uri, StringView tail)
{
    return uri.size > tail.size &&
        memcmp(uri.end() - tail.size, tail.data, tail.size) == 0 &&
        uri[uri.size - tail.size - 1] == '/'
        ? StringView(uri.begin(), uri.end() - tail.size)
        : nullptr;
}

const char *
RelocateUri(struct pool &pool, const char *uri,
            const char *internal_host, StringView internal_path,
            const char *external_scheme, const char *external_host,
            StringView external_path, StringView base)
{
    const char *path = MatchUriHost(uri, internal_host);
    if (path == nullptr)
        return nullptr;

    const StringView tail = UriBaseTail(external_path, base);
    if (tail.IsNull())
        return nullptr;

    const StringView prefix = UriPrefixBeforeTail(internal_path, tail);
    if (prefix.IsNull())
        return nullptr;

    const StringView tail2 = UriBaseTail(path, prefix);
    if (tail2.IsNull())
        return nullptr;

    return p_strncat(&pool, external_scheme, strlen(external_scheme),
                     "://", size_t(3),
                     external_host, strlen(external_host),
                     base.data, base.size,
                     tail2.data, tail2.size,
                     nullptr);
}
