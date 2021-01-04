/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "ua_classification.hxx"
#include "system/Error.hxx"
#include "util/StringStrip.hxx"
#include "util/CharUtil.hxx"
#include "util/ScopeExit.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <stdio.h>

static bool
parse_line(UserAgentClass &cls, char *line)
{
    if (*line == 'm')
        ++line;
    else if (*line != '/')
        throw std::runtime_error("Regular expression must start with '/' or 'm'");

    char delimiter = *line++;
    const char *r = line;
    char *end = strchr(line, delimiter);
    if (end == nullptr)
        throw std::runtime_error("Regular expression not terminated");

    *end = 0;
    line = StripLeft(end + 1);

    const char *name = line++;
    if (!IsAlphaNumericASCII(*name))
        throw std::runtime_error("Alphanumeric class name expected");

    while (IsAlphaNumericASCII(*line))
        ++line;

    if (*line != 0) {
        if (!IsWhitespaceFast(*line))
            throw std::runtime_error("Alphanumeric class name expected");

        *line++ = 0;
        line = StripLeft(line);

        if (*line != 0)
            throw std::runtime_error("Excess characters after class name");
    }

    cls.regex.Compile(r, false, false);

    cls.name = name;
    return true;
}

static std::forward_list<UserAgentClass>
ua_classification_init(FILE *file)
{
    std::forward_list<UserAgentClass> list;
    auto tail = list.before_begin();

    char line[1024];
    while (fgets(line, sizeof(line), file) != nullptr) {
        char *p = StripLeft(line);

        if (*p == 0 || *p == '#')
            continue;

        UserAgentClass cls;
        parse_line(cls, p);

        tail = list.emplace_after(tail, std::move(cls));
    }

    return list;
}

UserAgentClassList
ua_classification_init(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == nullptr)
        throw FormatErrno("Failed to open %s", path);

    AtScopeExit(file) { fclose(file); };

    return UserAgentClassList(ua_classification_init(file));
}

const char *
UserAgentClassList::Lookup(const char *user_agent) const noexcept
{
    assert(user_agent != nullptr);

    for (const auto &i : list)
        if (i.regex.Match(user_agent))
            return i.name.c_str();

    return nullptr;
}
