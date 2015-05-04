/*
 * Utilities for host names
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hostname.hxx"
#include "util/CharUtil.hxx"

static constexpr bool
valid_hostname_char(char ch)
{
    /* we are allowing ':' here because it is a valid character in the
       HTTP "Host" request header */

    return IsAlphaNumericASCII(ch) || ch == '-' || ch == '.' || ch == ':';
}

bool
hostname_is_well_formed(const char *p)
{
    if (*p == 0)
        return false;

    do {
        if (!valid_hostname_char(*p))
            return false;

        ++p;
    } while (*p != 0);

    return true;
}
