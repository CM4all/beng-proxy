/*
 * Handle cookies sent by the HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie-server.h"
#include "strref2.h"
#include "strref-pool.h"
#include "strmap.h"
#include "http-string.h"

#include <inline/list.h>

void
cookie_map_parse(struct strmap *cookies, const char *p, pool_t pool)
{
    struct strref input, name, value;

    assert(cookies != NULL);
    assert(p != NULL);

    strref_set_c(&input, p);

    while (1) {
        http_next_name_value(pool, &input, &name, &value);
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
