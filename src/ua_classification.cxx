/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ua_classification.hxx"
#include "regex.hxx"
#include "gerrno.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct UserAgentClass {
    GRegex *regex;
    char *name;
};

static UserAgentClass ua_classes[64];
static unsigned num_ua_classes;

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
    line = end + 1;
    while (*line != 0 && g_ascii_isspace(*line))
        ++line;

    const char *name = line++;
    if (!g_ascii_isalnum(*name)) {
        g_set_error(error_r, ua_classification_quark(), 0,
                    "Alphanumeric class name expected");
        return false;
    }

    while (g_ascii_isalnum(*line))
        ++line;

    if (*line != 0) {
        if (!g_ascii_isspace(*line)) {
            g_set_error(error_r, ua_classification_quark(), 0,
                        "Alphanumeric class name expected");
            return false;
        }

        *line++ = 0;
        while (*line != 0 && g_ascii_isspace(*line))
            ++line;

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

    cls.name = g_strdup(name);
    return true;
}

static bool
ua_classification_init(FILE *file, GError **error_r)
{
    char line[1024];
    while (fgets(line, G_N_ELEMENTS(line), file) != nullptr) {
        char *p = line;
        while (*p != 0 && g_ascii_isspace(*p))
            ++p;

        if (*p == 0 || *p == '#')
            continue;

        if (num_ua_classes >= G_N_ELEMENTS(ua_classes)) {
            g_set_error(error_r, ua_classification_quark(), 0,
                        "Too many UA classes");
            return false;
        }

        if (!parse_line(ua_classes[num_ua_classes], p, error_r))
            return false;

        ++num_ua_classes;
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

    bool success = ua_classification_init(file, error_r);
    fclose(file);
    return success;
}

void
ua_classification_deinit()
{
    for (UserAgentClass *i = ua_classes, *end = ua_classes + num_ua_classes;
         i != end; ++i) {
        g_regex_unref(i->regex);
        g_free(i->name);
    }
}

gcc_pure
const char *
ua_classification_lookup(const char *user_agent)
{
    assert(user_agent != nullptr);

    for (UserAgentClass *i = ua_classes, *end = ua_classes + num_ua_classes;
         i != end; ++i)
        if (g_regex_match(i->regex, user_agent, GRegexMatchFlags(0), nullptr))
            return i->name;

    return nullptr;
}
