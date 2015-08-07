/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ua_classification.hxx"
#include "regex.hxx"
#include "util/CharUtil.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"

#include <forward_list>
#include <string>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static constexpr Domain ua_classification_domain("ua_classification");

struct UserAgentClass {
    UniqueRegex regex;
    std::string name;
};

typedef std::forward_list<UserAgentClass> UserAgentClassList;

static UserAgentClassList *ua_classes;

gcc_pure
static char *
StripLeft(char *p)
{
    while (IsWhitespaceNotNull(*p))
        ++p;

    return p;
}

static bool
parse_line(UserAgentClass &cls, char *line, Error &error)
{
    if (*line == 'm')
        ++line;
    else if (*line != '/') {
        error.Set(ua_classification_domain,
                  "Regular expression must start with '/' or 'm'");
        return false;
    }

    char delimiter = *line++;
    const char *r = line;
    char *end = strchr(line, delimiter);
    if (end == nullptr) {
        error.Set(ua_classification_domain,
                  "Regular expression not terminated");
        return false;
    }

    *end = 0;
    line = StripLeft(end + 1);

    const char *name = line++;
    if (!IsAlphaNumericASCII(*name)) {
        error.Set(ua_classification_domain,
                  "Alphanumeric class name expected");
        return false;
    }

    while (IsAlphaNumericASCII(*line))
        ++line;

    if (*line != 0) {
        if (!IsWhitespaceFast(*line)) {
            error.Set(ua_classification_domain,
                      "Alphanumeric class name expected");
            return false;
        }

        *line++ = 0;
        line = StripLeft(line);

        if (*line != 0) {
            error.Set(ua_classification_domain,
                      "Excess characters after class name");
            return false;
        }
    }

    if (!cls.regex.Compile(r, false, error))
        return false;

    cls.name = name;
    return true;
}

static bool
ua_classification_init(UserAgentClassList &list, FILE *file, Error &error)
{
    auto tail = ua_classes->before_begin();

    char line[1024];
    while (fgets(line, sizeof(line), file) != nullptr) {
        char *p = StripLeft(line);

        if (*p == 0 || *p == '#')
            continue;

        UserAgentClass cls;
        if (!parse_line(cls, p, error))
            return false;

        tail = list.emplace_after(tail, std::move(cls));
    }

    return true;
}

bool
ua_classification_init(const char *path, Error &error)
{
    if (path == nullptr)
        return true;

    FILE *file = fopen(path, "r");
    if (file == nullptr) {
        error.FormatErrno("Failed to open %s", path);
        return false;
    }

    ua_classes = new UserAgentClassList();
    bool success = ua_classification_init(*ua_classes, file, error);
    fclose(file);
    if (!success)
        ua_classification_deinit();
    return success;
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
