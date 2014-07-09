/*
 * Connect to one of a list of addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLIENT_BALANCER_HXX
#define BENG_PROXY_CLIENT_BALANCER_HXX

#include <stddef.h>

struct pool;
struct balancer;
struct address_list;
struct ConnectSocketHandler;
struct async_operation_ref;
struct sockaddr;

/**
 * Open a connection to any address in the specified address list.
 * This is done in a round-robin fashion, ignoring hosts that are
 * known to be down.
 *
 * @param timeout the connect timeout for each attempt [seconds]
 */
void
client_balancer_connect(struct pool *pool, struct balancer *balancer,
                        bool ip_transparent,
                        const struct sockaddr *bind_address,
                        size_t bind_address_size,
                        unsigned session_sticky,
                        const struct address_list *address_list,
                        unsigned timeout,
                        const ConnectSocketHandler *handler, void *ctx,
                        struct async_operation_ref *async_ref);

#endif
