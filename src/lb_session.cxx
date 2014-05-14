/*
 * Session handling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_session.hxx"
#include "strmap.h"
#include "tpool.h"
#include "cookie_server.hxx"

#include <string.h>
#include <stdlib.h>

unsigned
lb_session_get(const struct strmap *request_headers,
                        const char *cookie_name)
{
    const AutoRewindPool auto_rewind(tpool);

    const char *cookie = strmap_get(request_headers, "cookie");
    if (cookie == NULL)
        return 0;

    struct strmap *jar = strmap_new(tpool, 8);
    cookie_map_parse(jar, cookie, tpool);

    const char *session = strmap_get(jar, cookie_name);
    if (session == NULL)
        return 0;

    size_t length = strlen(session);
    if (length > 8)
        /* only parse the upper 32 bits */
        session += length - 8;

    char *endptr;
    unsigned long id = strtoul(session, &endptr, 16);
    if (endptr == session || *endptr != 0)
        return 0;

    return (unsigned)id;
}
