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

#include "uri_extract.hxx"
#include "util/StringView.hxx"
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
IsValidScheme(StringView p)
{
    if (p.IsEmpty() || !IsValidSchemeStart(p.front()))
        return false;

    for (size_t i = 1; i < p.size; ++i)
        if (!IsValidSchemeChar(p[i]))
            return false;

    return true;
}

bool
uri_has_protocol(StringView uri)
{
    assert(!uri.IsNull());

    const char *colon = uri.Find(':');
    return colon != nullptr &&
        IsValidScheme({uri.data, colon}) &&
        colon < uri.data + uri.size - 2 &&
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

    const char *colon = strchr(uri, ':');
    return colon != nullptr &&
        IsValidScheme({uri, colon}) &&
        colon[1] == '/' && colon[2] == '/'
        ? colon + 3
        : nullptr;
}

gcc_pure
static const char *
uri_after_protocol(StringView uri)
{
    if (uri.size > 2 && uri[0] == '/' && uri[1] == '/' && uri[2] != '/')
        return uri.data + 2;

    const char *colon = uri.Find(':');
    return colon != nullptr &&
        IsValidScheme({uri.data, colon}) &&
        colon < uri.data + uri.size - 2 &&
        colon[1] == '/' && colon[2] == '/'
        ? colon + 3
        : nullptr;
}

bool
uri_has_authority(StringView uri)
{
    return uri_after_protocol(uri) != nullptr;
}

StringView
uri_host_and_port(const char *uri)
{
    assert(uri != nullptr);

    uri = uri_after_protocol(uri);
    if (uri == nullptr)
        return nullptr;

    const char *slash = strchr(uri, '/');
    if (slash == nullptr)
        return uri;

    return { uri, size_t(slash - uri) };
}

const char *
uri_path(const char *uri)
{
    assert(uri != nullptr);

    const char *ap = uri_after_protocol(uri);
    if (ap != nullptr)
        return strchr(ap, '/');

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
