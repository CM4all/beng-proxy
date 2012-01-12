/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address-resolver.h"
#include "address-list.h"

#include <socket/resolver.h>

#include <assert.h>
#include <netdb.h>

bool
address_list_resolve(struct pool *pool, struct address_list *address_list,
                     const char *host_and_port, int default_port,
                     const struct addrinfo *hints,
                     GError **error_r)
{
    assert(pool != NULL);
    assert(address_list != NULL);

    struct addrinfo *ai;
    int ret = socket_resolve_host_port(host_and_port, default_port,
                                       hints, &ai);
    if (ret != 0) {
        g_set_error(error_r, resolver_quark(), ret,
                    "Failed to resolve '%s': %s",
                    host_and_port, gai_strerror(ret));
        return false;
    }

    for (const struct addrinfo *i = ai; i != NULL; i = i->ai_next)
        address_list_add(pool, address_list, i->ai_addr, i->ai_addrlen);

    freeaddrinfo(ai);

    return true;
}
