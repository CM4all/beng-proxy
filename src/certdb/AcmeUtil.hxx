/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ACME_UTIL_HXX
#define BENG_PROXY_ACME_UTIL_HXX

#include "util/StringView.hxx"

#include <string>

static bool
IsAcmeInvalid(StringView s)
{
    return s.EndsWith(".acme.invalid");
}

static bool
IsAcmeInvalid(const std::string &s)
{
    return IsAcmeInvalid(StringView(s.data(), s.length()));
}

#endif
