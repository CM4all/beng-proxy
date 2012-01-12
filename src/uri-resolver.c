/*
 * Resolve a "host[:port]" specification, and store it in an
 * uri_with_address object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-resolver.h"
#include "uri-address.h"
#include "address-resolver.h"

#include <daemon/log.h>

struct uri_with_address *
uri_address_new_resolve(struct pool *pool, const char *host_and_port,
                        int default_port, const struct addrinfo *hints)
{
    struct uri_with_address *uwa = uri_address_new(pool, host_and_port);

    GError *error = NULL;
    if (!address_list_resolve(pool, &uwa->addresses,
                              host_and_port, default_port, hints,
                              &error)) {
        daemon_log(1, "%s\n", error->message);
        g_error_free(error);
        return NULL;
    }

    return uwa;
}
