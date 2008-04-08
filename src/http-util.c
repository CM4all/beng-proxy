/*
 * Various utilities for working with HTTP objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-util.h"
#include "strutil.h"
#include "strref.h"

#include <string.h>

int
http_list_contains(const char *list, const char *item)
{
    const char *comma;

    while (*list != 0) {
        comma = strchr(list, ',');
        if (comma == NULL)
            return strcmp(list, item) == 0;

        if (memcmp(list, item, comma - list) == 0)
            return 1;

        list = comma + 1;
    }

    return 0;
}

struct strref *
http_header_param(struct strref *dest, const char *value, const char *name)
{
    /* XXX this implementation only supports one param */
    const char *p = strchr(value, ';'), *q;

    if (p == NULL)
        return NULL;

    ++p;

    while (*p != 0 && char_is_whitespace(*p))
        ++p;

    q = strchr(p, '=');
    if (q == NULL || (size_t)(q - p) != strlen(name) ||
        memcmp(p, name, q - p) != 0)
        return NULL;

    p = q + 1;
    if (*p == '"') {
        ++p;
        q = strchr(p, '"');
        if (q == NULL)
            strref_set_c(dest, p);
        else
            strref_set(dest, p, q - p);
    } else {
        strref_set_c(dest, p);
    }

    return dest;
}
