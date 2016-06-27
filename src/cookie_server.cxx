/*
 * Handle cookies sent by the HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_server.hxx"
#include "cookie_string.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "util/StringView.hxx"

StringMap
cookie_map_parse(struct pool &pool, const char *p)
{
    assert(p != nullptr);

    StringMap cookies(pool);

    StringView input = p;

    while (true) {
        StringView name, value;
        cookie_next_name_value(pool, input, name, value, true);
        if (name.IsEmpty())
            break;

        cookies.Add(p_strdup(pool, name), p_strdup(pool, value));

        input.StripLeft();
        if (input.IsEmpty() || input.front() != ';')
            break;

        input.pop_front();
        input.StripLeft();
    }

    return cookies;
}

const char *
cookie_exclude(const char *p, const char *_exclude, struct pool *pool)
{
    assert(p != nullptr);
    assert(_exclude != nullptr);

    const char *const p0 = p;
    char *const dest0 = (char *)p_malloc(pool, strlen(p) + 1);
    char *dest = dest0;

    StringView input = p;

    const StringView exclude = _exclude;
    const char *src = p;

    bool empty = true, found = false;

    while (true) {
        StringView name, value;
        cookie_next_name_value(*pool, input, name, value, true);
        if (name.IsEmpty())
            break;

        const bool skip = name.Equals(exclude);
        if (skip) {
            found = true;
            dest = (char *)mempcpy(dest, src, name.data - src);
        } else
            empty = false;

        input.StripLeft();
        if (input.IsEmpty() || input.front() != ';') {
            if (skip)
                src = input.data;
            break;
        }

        input.pop_front();
        input.StripLeft();

        if (skip)
            src = input.data;
    }

    if (!found)
        return p0;

    if (empty)
        return nullptr;

    dest = (char *)mempcpy(dest, src, input.data - src);
    *dest = 0;
    return dest0;
}
