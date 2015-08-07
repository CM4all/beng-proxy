/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ua_classification.hxx"
#include "regex.hxx"
#include "gerrno.h"
#include "util/CharUtil.hxx"

#include <forward_list>
#include <string>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct UserAgentClass {
    GRegex *regex;
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
parse_line(UserAgentClass &cls, char *line, GError **error_r)
{
    if (*line == 'm')
        ++line;
    else if (*line != '/') {
        g_set_error(error_r, ua_classification_quark(), 0,
                    "Regular expression must start with '/' or 'm'");
        return false;
    }

    char delimiter = *line++;
    const char *r = line;
    char *end = strchr(line, delimiter);
    if (end == nullptr) {
        g_set_error(error_r, ua_classification_quark(), 0,
                    "Regular expression not terminated");
        return false;
    }

    *end = 0;
    line = StripLeft(end + 1);

    const char *name = line++;
    if (!IsAlphaNumericASCII(*name)) {
        g_set_error(error_r, ua_classification_quark(), 0,
                    "Alphanumeric class name expected");
        return false;
    }

    while (IsAlphaNumericASCII(*line))
        ++line;

    if (*line != 0) {
        if (!IsWhitespaceFast(*line)) {
            g_set_error(error_r, ua_classification_quark(), 0,
                        "Alphanumeric class name expected");
            return false;
        }

        *line++ = 0;
        line = StripLeft(line);

        if (*line != 0) {
            g_set_error(error_r, ua_classification_quark(), 0,
                        "Excess characters after class name");
            return false;
        }
    }

    constexpr GRegexCompileFlags compile_flags =
        GRegexCompileFlags(G_REGEX_MULTILINE|G_REGEX_DOTALL|
                           G_REGEX_RAW|G_REGEX_NO_AUTO_CAPTURE|
                           G_REGEX_OPTIMIZE);
    cls.regex = g_regex_new(r, compile_flags, GRegexMatchFlags(0), error_r);
    if (cls.regex == nullptr)
        return false;

    cls.name = name;
    return true;
}

static bool
ua_classification_init(UserAgentClassList &list, FILE *file, GError **error_r)
{
    auto tail = ua_classes->before_begin();

    char line[1024];
    while (fgets(line, sizeof(line), file) != nullptr) {
        char *p = StripLeft(line);

        if (*p == 0 || *p == '#')
            continue;

        UserAgentClass cls;
        if (!parse_line(cls, p, error_r))
            return false;

        tail = list.emplace_after(tail, std::move(cls));
    }

    return true;
}

bool
ua_classification_init(const char *path, GError **error_r)
{
    if (path == nullptr)
        return true;

    FILE *file = fopen(path, "r");
    if (file == nullptr) {
        g_set_error(error_r, errno_quark(), errno,
                    "Failed to open %s: %s", path, g_strerror(errno));
        return false;
    }

    ua_classes = new UserAgentClassList();
    bool success = ua_classification_init(*ua_classes, file, error_r);
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

    for (auto &i : *ua_classes) {
        g_regex_unref(i.regex);
    }

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
        if (g_regex_match(i.regex, user_agent, GRegexMatchFlags(0), nullptr))
            return i.name.c_str();

    return nullptr;
}
