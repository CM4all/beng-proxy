/*
 * Various utilities for working with HTTP objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_util.hxx"
#include "strutil.h"
#include "strref.h"
#include "pool.h"

#include <string.h>

char **
http_list_split(struct pool *pool, const char *p)
{
    enum { MAX_ITEMS = 64 };
    char *tmp[MAX_ITEMS + 1]; /* XXX dynamic allocation */
    size_t num = 0;

    do {
        const char *comma, *end;

        /* skip whitespace */
        while (*p != 0 && char_is_whitespace(*p))
            ++p;

        if (*p == 0)
            break;

        /* find the next delimiter */
        end = comma = strchr(p, ',');
        if (end == nullptr)
            /* last element */
            end = p + strlen(p);

        /* delete trailing whitespace */
        while (end > p && char_is_whitespace(end[-1]))
            --end;

        /* append new list item */
        tmp[num] = p_strndup(pool, p, end - p);
        str_to_lower(tmp[num++]);

        if (comma == nullptr)
            /* this was the last element */
            break;

        /* continue after the comma */
        p = comma + 1;
    } while (num < MAX_ITEMS);

    tmp[num++] = nullptr;

    return (char**)p_memdup(pool, tmp, num * sizeof(tmp[0]));
}

static void
http_trim(const char **pp, size_t *length_p)
{
    const char *p = *pp;
    size_t length = *length_p;

    /* trim whitespace */

    while (length > 0 && char_is_whitespace(p[length - 1]))
        --length;

    while (length > 0 && char_is_whitespace(p[0])) {
        ++p;
        --length;
    }

    /* remove quotes from quoted-string */

    if (length >= 2 && p[0] == '"' && p[length - 1] == '"') {
        ++p;
        length -= 2;
    }

    /* return */

    *pp = p;
    *length_p = length;
}

static bool
http_equals(const char *a, size_t a_length, const char *b, size_t b_length)
{
    http_trim(&a, &a_length);
    http_trim(&b, &b_length);

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
        if (comma == nullptr)
            return http_equals(list, strlen(list), item, item_length);

        if (http_equals(list, comma - list, item, item_length))
            return true;

        list = comma + 1;
    }

    return false;
}

static bool
http_equals_i(const char *a, size_t a_length, const char *b, size_t b_length)
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

    return a_length == b_length && strncasecmp(a, b, a_length) == 0;
}

bool
http_list_contains_i(const char *list, const char *item)
{
    const char *comma;
    size_t item_length = strlen(item);

    while (*list != 0) {
        /* XXX what if the comma is within an quoted-string? */
        comma = strchr(list, ',');
        if (comma == nullptr)
            return http_equals(list, strlen(list), item, item_length);

        if (http_equals_i(list, comma - list, item, item_length))
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

    if (p == nullptr)
        return nullptr;

    ++p;

    while (*p != 0 && char_is_whitespace(*p))
        ++p;

    q = strchr(p, '=');
    if (q == nullptr || (size_t)(q - p) != strlen(name) ||
        memcmp(p, name, q - p) != 0)
        return nullptr;

    p = q + 1;
    if (*p == '"') {
        ++p;
        q = strchr(p, '"');
        if (q == nullptr)
            strref_set_c(dest, p);
        else
            strref_set(dest, p, q - p);
    } else {
        strref_set_c(dest, p);
    }

    return dest;
}
