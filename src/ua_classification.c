/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ua_classification.h"
#include "gerrno.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct ua_class {
    GRegex *regex;
    char *name;
};

static struct ua_class ua_classes[64];
static unsigned num_ua_classes;

static bool
parse_line(struct ua_class *class, char *line, GError **error_r)
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
    if (end == NULL) {
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

    GRegexCompileFlags compile_flags =
        G_REGEX_MULTILINE|G_REGEX_DOTALL|
        G_REGEX_RAW|G_REGEX_NO_AUTO_CAPTURE|
        G_REGEX_OPTIMIZE;
    class->regex = g_regex_new(r, compile_flags, 0, error_r);
    if (class->regex == NULL)
        return false;

    class->name = g_strdup(name);
    return true;
}

bool
ua_classification_init(const char *path, GError **error_r)
{
    if (path == NULL)
        return true;

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        g_set_error(error_r, errno_quark(), errno,
                    "Failed to open %s: %s", path, g_strerror(errno));
        return false;
    }

    char line[1024];
    while (fgets(line, G_N_ELEMENTS(line), file) != NULL) {
        char *p = line;
        while (*p != 0 && g_ascii_isspace(*p))
            ++p;

        if (*p == 0 || *p == '#')
            continue;

        if (num_ua_classes >= G_N_ELEMENTS(ua_classes)) {
            fclose(file);
            g_set_error(error_r, ua_classification_quark(), 0,
                        "Too many UA classes");
            return false;
        }

        if (!parse_line(&ua_classes[num_ua_classes], p, error_r)) {
            fclose(file);
            return false;
        }

        ++num_ua_classes;
    }

    fclose(file);
    return true;
}

void
ua_classification_deinit(void)
{
    for (struct ua_class *i = ua_classes, *end = ua_classes + num_ua_classes;
         i != end; ++i) {
        g_regex_unref(i->regex);
        g_free(i->name);
    }
}

gcc_pure
const char *
ua_classification_lookup(const char *user_agent)
{
    assert(user_agent != NULL);

    for (struct ua_class *i = ua_classes, *end = ua_classes + num_ua_classes;
         i != end; ++i)
        if (g_regex_match(i->regex, user_agent, 0, NULL))
            return i->name;

    return NULL;
}
