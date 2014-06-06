/*
 * Handle cookies sent by the HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_server.hxx"
#include "cookie_string.hxx"
#include "strref2.h"
#include "strref-pool.h"
#include "strmap.h"
#include "pool.h"

#include <inline/list.h>

void
cookie_map_parse(struct strmap *cookies, const char *p, struct pool *pool)
{
    assert(cookies != nullptr);
    assert(p != nullptr);

    struct strref input;
    strref_set_c(&input, p);

    while (true) {
        struct strref name, value;
        cookie_next_name_value(pool, &input, &name, &value, true);
        if (strref_is_empty(&name))
            break;

        strmap_add(cookies, strref_dup(pool, &name), strref_dup(pool, &value));

        strref_ltrim(&input);
        if (strref_is_empty(&input) || input.data[0] != ';')
            break;

        strref_skip(&input, 1);
        strref_ltrim(&input);
    }
}

const char *
cookie_exclude(const char *p, const char *exclude, struct pool *pool)
{
    assert(p != nullptr);
    assert(exclude != nullptr);

    const char *const p0 = p;
    char *const dest0 = (char *)p_malloc(pool, strlen(p) + 1);
    char *dest = dest0;

    struct strref input;
    strref_set_c(&input, p);

    const size_t exclude_length = strlen(exclude);
    const char *src = p;

    bool empty = true, found = false;

    while (true) {
        struct strref name, value;
        cookie_next_name_value(pool, &input, &name, &value, true);
        if (strref_is_empty(&name))
            break;

        bool skip = strref_cmp(&name, exclude, exclude_length) == 0;
        if (skip) {
            found = true;
            dest = (char *)mempcpy(dest, src, name.data - src);
        } else
            empty = false;

        strref_ltrim(&input);
        if (strref_is_empty(&input) || input.data[0] != ';') {
            if (skip)
                src = input.data;
            break;
        }

        strref_skip(&input, 1);
        strref_ltrim(&input);

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
