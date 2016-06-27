/*
 * Session handling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_session.hxx"
#include "strmap.hxx"
#include "tpool.hxx"
#include "cookie_server.hxx"
#include "pool.hxx"

#include <string.h>
#include <stdlib.h>

unsigned
lb_session_get(const StringMap &request_headers, const char *cookie_name)
{
    const AutoRewindPool auto_rewind(*tpool);

    const char *cookie = request_headers.Get("cookie");
    if (cookie == NULL)
        return 0;

    const auto jar = cookie_map_parse(*tpool, cookie);

    const char *session = jar.Get(cookie_name);
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
