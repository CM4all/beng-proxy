/*
 * Various utilities for working with HTTP objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_util.hxx"
#include "pool.hxx"
#include "util/StringUtil.hxx"
#include "util/StringView.hxx"

#include <string.h>

char **
http_list_split(struct pool *pool, const char *p)
{
    constexpr size_t MAX_ITEMS = 64;
    char *tmp[MAX_ITEMS + 1]; /* XXX dynamic allocation */
    size_t num = 0;

    do {
        const char *comma, *end;

        /* skip whitespace */
        p = StripLeft(p);

        if (*p == 0)
            break;

        /* find the next delimiter */
        end = comma = strchr(p, ',');
        if (end == nullptr)
            /* last element */
            end = p + strlen(p);

        /* delete trailing whitespace */
        end = StripRight(p, end);

        /* append new list item */
        tmp[num++] = p_strdup_lower(*pool, StringView(p, end));

        if (comma == nullptr)
            /* this was the last element */
            break;

        /* continue after the comma */
        p = comma + 1;
    } while (num < MAX_ITEMS);

    tmp[num++] = nullptr;

    return (char**)p_memdup(pool, tmp, num * sizeof(tmp[0]));
}

static StringView
http_trim(StringView s)
{
    /* trim whitespace */

    s.Strip();

    /* remove quotes from quoted-string */

    if (s.size >= 2 && s.front() == '"' && s.back() == '"') {
        s.pop_front();
        s.pop_back();
    }

    /* return */

    return s;
}

static bool
http_equals(StringView a, StringView b)
{
    return http_trim(a).Equals(http_trim(b));
}

bool
http_list_contains(const char *list, const char *_item)
{
    const StringView item(_item);

    while (*list != 0) {
        /* XXX what if the comma is within an quoted-string? */
        const char *comma = strchr(list, ',');
        if (comma == nullptr)
            return http_equals(list, item);

        if (http_equals({list, comma}, item))
            return true;

        list = comma + 1;
    }

    return false;
}

static bool
http_equals_i(StringView a, StringView b)
{
    return http_trim(a).EqualsIgnoreCase(b);
}

bool
http_list_contains_i(const char *list, const char *_item)
{
    const StringView item(_item);

    while (*list != 0) {
        /* XXX what if the comma is within an quoted-string? */
        const char *comma = strchr(list, ',');
        if (comma == nullptr)
            return http_equals_i(list, item);

        if (http_equals_i({list, comma}, item))
            return true;

        list = comma + 1;
    }

    return false;
}

StringView
http_header_param(const char *value, const char *name)
{
    /* XXX this implementation only supports one param */
    const char *p = strchr(value, ';'), *q;

    if (p == nullptr)
        return nullptr;

    p = StripLeft(p + 1);

    q = strchr(p, '=');
    if (q == nullptr || (size_t)(q - p) != strlen(name) ||
        memcmp(p, name, q - p) != 0)
        return nullptr;

    p = q + 1;
    if (*p == '"') {
        ++p;
        q = strchr(p, '"');
        if (q == nullptr)
            return p;
        else
            return {p, size_t(q - p)};
    } else {
        return p;
    }
}
