/*
 * Various utilities for working with HTTP objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-util.h"
#include "strutil.h"
#include "strref.h"

#include <string.h>

static bool
http_equals(const char *a, size_t a_length, const char *b, size_t b_length)
{
    /* trim */

    while (a_length > 0 && char_is_whitespace(a[a_length - 1]))
        --a_length;

    while (a_length > 0 && char_is_whitespace(a[0])) {
        ++a;
        --a_length;
    }

    /* remove quotes from quoted-string */

    if (a_length >= 2 && a[0] == '"' && a[a_length - 1] == '"') {
        ++a;
        a_length -= 2;
    }

    /* finally compare */

    return a_length == b_length && memcmp(a, b, a_length) == 0;
}

bool
http_list_contains(const char *list, const char *item)
{
    const char *comma;
    size_t item_length = strlen(item);

    while (*list != 0) {
        /* XXX what if the comma is within an quoted-string? */
        comma = strchr(list, ',');
        if (comma == NULL)
            return http_equals(list, strlen(list), item, item_length);

        if (http_equals(list, comma - list, item, item_length))
            return true;

        list = comma + 1;
    }

    return false;
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
