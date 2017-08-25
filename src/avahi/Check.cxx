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

#include "Check.hxx"
#include "util/StringView.hxx"
#include "util/CharUtil.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <stdio.h>

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
        throw std::runtime_error("Service type must end with '._tcp' or '._udp'");

    CheckZeroconfServiceName({type + 1, type + length - 5});
}

std::string
MakeZeroconfServiceType(const char *value, const char *default_suffix)
{
    assert(value != nullptr);
    assert(default_suffix != nullptr);
    assert(strcmp(default_suffix, "_tcp") == 0 ||
           strcmp(default_suffix, "_udp") == 0);

    if (*value == '_' && strchr(value, '.') != nullptr) {
        /* this is a fully-qualified service type - validate it and
           return it as-is */
        CheckZeroconfServiceType(value);
        return value;
    } else {
        /* this is a bare service name - validate it and add
           prefix/suffix */
        CheckZeroconfServiceName(value);

        char buffer[32];
        snprintf(buffer, sizeof(buffer), "_%s.%s", value, default_suffix);
        return buffer;
    }
}
