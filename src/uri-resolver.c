/*
 * Resolve a "host[:port]" specification, and store it in an
 * uri_with_address object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-resolver.h"
#include "uri-address.h"

#include <daemon/log.h>
#include <socket/resolver.h>

#include <assert.h>
#include <netdb.h>

struct uri_with_address *
uri_address_new_resolve(pool_t pool, const char *host_and_port,
                        int default_port, const struct addrinfo *hints)
{
    int ret;
    struct addrinfo *ai;
    struct uri_with_address *uwa;

    ret = socket_resolve_host_port(host_and_port, default_port, hints, &ai);
    if (ret != 0) {
        daemon_log(1, "Failed to resolve '%s': %s\n",
                   host_and_port, gai_strerror(ret));
        return NULL;
    }

    assert(ai != NULL);

    uwa = uri_address_new(pool, host_and_port);

    for (const struct addrinfo *i = ai; i != NULL; i = i->ai_next)
        uri_address_add(uwa, i->ai_addr, i->ai_addrlen);

    freeaddrinfo(ai);

    return uwa;
}
