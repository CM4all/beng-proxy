/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ua_classification.hxx"
#include "regex.hxx"
#include "system/Error.hxx"
#include "util/StringUtil.hxx"
#include "util/CharUtil.hxx"
#include "util/ScopeExit.hxx"

#include <stdexcept>
#include <forward_list>
#include <string>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct UserAgentClass {
    UniqueRegex regex;
    std::string name;
};

typedef std::forward_list<UserAgentClass> UserAgentClassList;

static UserAgentClassList *ua_classes;

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

static void
ua_classification_init(UserAgentClassList &list, FILE *file)
{
    auto tail = ua_classes->before_begin();

    char line[1024];
    while (fgets(line, sizeof(line), file) != nullptr) {
        char *p = StripLeft(line);

        if (*p == 0 || *p == '#')
            continue;

        UserAgentClass cls;
        parse_line(cls, p);

        tail = list.emplace_after(tail, std::move(cls));
    }
}

void
ua_classification_init(const char *path)
{
    if (path == nullptr)
        return;

    FILE *file = fopen(path, "r");
    if (file == nullptr)
        throw FormatErrno("Failed to open %s", path);

    AtScopeExit(file) { fclose(file); };

    ua_classes = new UserAgentClassList();
    try {
        ua_classification_init(*ua_classes, file);
    } catch (...) {
        ua_classification_deinit();
        throw;
    }
}

void
ua_classification_deinit()
{
    if (ua_classes == nullptr)
        return;

    delete ua_classes;
    ua_classes = nullptr;
}

gcc_pure
const char *
ua_classification_lookup(const char *user_agent)
{
    assert(user_agent != nullptr);

    if (ua_classes == nullptr)
        return nullptr;

    for (const auto &i : *ua_classes)
        if (i.regex.Match(user_agent))
            return i.name.c_str();

    return nullptr;
}
