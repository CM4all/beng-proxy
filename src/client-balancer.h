/*
 * Connect to one of a list of addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLIENT_BALANCER_H
#define BENG_PROXY_CLIENT_BALANCER_H

struct pool;
struct balancer;
struct address_list;
struct client_socket_handler;
struct async_operation_ref;

/**
 * Open a connection to any address in the specified address list.
 * This is done in a round-robin fashion, ignoring hosts that are
 * known to be down.
 *
 * @param timeout the connect timeout for each attempt [seconds]
 */
void
client_balancer_connect(struct pool *pool, struct balancer *balancer,
                        unsigned session_sticky,
                        const struct address_list *address_list,
                        unsigned timeout,
                        const struct client_socket_handler *handler, void *ctx,
                        struct async_operation_ref *async_ref);

#endif
