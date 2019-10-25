/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "relocate_uri.hxx"
#include "http_address.hxx"
#include "pool/pool.hxx"
#include "uri/Extract.hxx"
#include "util/StringView.hxx"

/**
 * If the given URI matches the #HttpAddress regarding and port, then
 * return the URI path.  If not, return nullptr.
 */
gcc_pure
static const char *
MatchUriHost(const char *uri, const char *host) noexcept
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
UriBaseTail(StringView uri, StringView base) noexcept
{
    return uri.StartsWith(base)
        ? StringView(uri.data + base.size, uri.end())
        : nullptr;
}

gcc_pure
static StringView
UriPrefixBeforeTail(StringView uri, StringView tail) noexcept
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
            StringView external_path, StringView base) noexcept
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
