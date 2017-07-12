/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Check.hxx"
#include "util/StringView.hxx"
#include "util/CharUtil.hxx"

#include <stdexcept>

#include <string.h>

void
CheckZeroconfServiceName(StringView name)
{
    if (name.size < 1)
        throw std::runtime_error("Service name must not be empty");

    if (name.size > 15)
        throw std::runtime_error("Service name must not be longer than 15 characters");

    bool found_letter = false;

    for (const char ch : name) {
        if (IsAlphaASCII(ch))
            found_letter = true;
        else if (!IsDigitASCII(ch) && ch != '-')
            throw std::runtime_error("Service name may contain only ASCII letters, digits and hyphens");
    }

    if (!found_letter)
        throw std::runtime_error("Service must contain at least one letter");
}

void
CheckZeroconfServiceType(const char *type)
{
    size_t length = strlen(type);

    if (*type != '_')
        throw std::runtime_error("Service type must begin with an underscore");

    if (length < 5 || (memcmp(type + length - 5, "._udp", 5) != 0 &&
                       memcmp(type + length - 5, "._tcp", 5)))
        throw std::runtime_error("Service type must end with '._tcp' oder '._udp'");

    CheckZeroconfServiceName({type + 1, type + length - 5});
}
