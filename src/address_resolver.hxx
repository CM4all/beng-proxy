/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_RESOLVER_HXX
#define BENG_PROXY_ADDRESS_RESOLVER_HXX

#include "glibfwd.hxx"

struct pool;
struct AddressList;
struct addrinfo;

/**
 * Resolve a "host[:port]" specification, and add all addresses to the
 * specified #address_list.
 */
bool
address_list_resolve(struct pool *pool, AddressList *address_list,
                     const char *host_and_port, int default_port,
                     const struct addrinfo *hints,
                     GError **error_r);

/**
 * Wrapper for address_list_resolve() that allocates a new
 * #address_list object from the pool instead of manipulating one that
 * was passed by the caller.
 */
AddressList *
address_list_resolve_new(struct pool *pool,
                         const char *host_and_port, int default_port,
                         const struct addrinfo *hints,
                         GError **error_r);

#endif
