/*
 * Handle cookies sent by the HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie-server.h"
#include "strref2.h"
#include "strmap.h"
#include "http-string.h"

#include <inline/list.h>

static void
parse_key_value(pool_t pool, struct strref *input,
                struct strref *name, struct strref *value)
{
    http_next_token(input, name);
    if (strref_is_empty(name))
        return;

    strref_ltrim(input);
    if (!strref_is_empty(input) && input->data[0] == '=') {
        strref_skip(input, 1);
        strref_ltrim(input);
        http_next_value(pool, input, value);
    } else
        strref_clear(value);
}

void
cookie_map_parse(struct strmap *cookies, const char *p, pool_t pool)
{
    struct strref input, name, value;

    assert(cookies != NULL);
    assert(p != NULL);

    strref_set_c(&input, p);

    while (1) {
        parse_key_value(pool, &input, &name, &value);
        if (strref_is_empty(&name))
            break;

        strmap_addn(cookies, strref_dup(pool, &name), strref_dup(pool, &value));

        strref_ltrim(&input);
        if (strref_is_empty(&input) || input.data[0] != ';')
            break;
    }
}
