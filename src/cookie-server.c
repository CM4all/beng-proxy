/*
 * Cookie management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie-server.h"
#include "strutil.h"
#include "strmap.h"
#include "http-string.h"

#include <inline/list.h>

void
cookie_map_parse(struct strmap *cookies, const char *p, pool_t pool)
{
    const char *name, *value, *end;

    assert(cookies != NULL);
    assert(p != NULL);

    while (1) {
        value = strchr(p, '=');
        if (value == NULL)
            break;

        name = p_strndup(pool, p, value - p);

        ++value;

        if (*value == '"') {
            ++value;

            end = strchr(value, '"');
            if (end == NULL)
                break;

            value = p_strndup(pool, value, end - value);

            end = strchr(value, ';');
        } else {
            end = strchr(value, ';');

            if (end == NULL)
                value = p_strdup(pool, value);
            else
                value = p_strndup(pool, value, end - value);
        }

        strmap_addn(cookies, name, value);

        if (end == NULL)
            break;

        p = end + 1;
        while (*p != 0 && char_is_whitespace(*p))
            ++p;
    }
}
