/*
 * Node selection by cookie.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Cookie.hxx"
#include "strmap.hxx"
#include "tpool.hxx"
#include "cookie_server.hxx"
#include "pool.hxx"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

sticky_hash_t
lb_cookie_get(const StringMap &request_headers)
{
    const AutoRewindPool auto_rewind(*tpool);

    const char *cookie = request_headers.Get("cookie");
    if (cookie == NULL)
        return 0;

    const auto jar = cookie_map_parse(*tpool, cookie);

    const char *p = jar.Get("beng_lb_node");
    if (p == NULL || memcmp(p, "0-", 2) != 0)
        return 0;

    p += 2;

    char *endptr;
    unsigned long id = strtoul(p, &endptr, 16);
    if (endptr == p || *endptr != 0)
        return 0;

    return (sticky_hash_t)id;
}

sticky_hash_t
lb_cookie_generate(unsigned n)
{
    assert(n >= 2);

    return (random() % n) + 1;
}
