/*
 * Resolve a "host[:port]" specification, and store it in an
 * uri_with_address object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_RESOLVER_H
#define BENG_PROXY_URI_RESOLVER_H

#include "pool.h"

struct uri_with_address;
struct addrinfo;

struct uri_with_address *
uri_address_new_resolve(pool_t pool, const char *host_and_port,
                        int default_port, const struct addrinfo *hints);

#endif
