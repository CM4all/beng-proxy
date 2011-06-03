/*
 * Node selection by cookie.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_cookie.h"
#include "strmap.h"
#include "tpool.h"
#include "cookie-server.h"

#include <string.h>
#include <stdlib.h>

static unsigned
lb_cookie_get_internal(const struct strmap *request_headers)
{
    const char *cookie = strmap_get(request_headers, "cookie");
    if (cookie == NULL)
        return 0;

    struct strmap *jar = strmap_new(tpool, 31);
    cookie_map_parse(jar, cookie, tpool);

    const char *p = strmap_get(jar, "beng_lb_node");
    if (p == NULL || memcmp(p, "0-", 2) != 0)
        return 0;

    p += 2;

    char *endptr;
    unsigned long id = strtoul(p, &endptr, 16);
    if (endptr == p || *endptr != 0)
        return 0;

    return (unsigned)id;
}

unsigned
lb_cookie_get(const struct strmap *request_headers)
{
    struct pool_mark mark;
    pool_mark(tpool, &mark);

    unsigned id = lb_cookie_get_internal(request_headers);
    pool_rewind(tpool, &mark);

    return id;
}

unsigned
lb_cookie_generate(unsigned n)
{
    return (random() % n) + 1;
}
