/*
 * Connect to one of a list of addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLIENT_BALANCER_HXX
#define BENG_PROXY_CLIENT_BALANCER_HXX

struct pool;
struct balancer;
struct AddressList;
struct ConnectSocketHandler;
struct async_operation_ref;
class SocketAddress;

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
                        SocketAddress bind_address,
                        unsigned session_sticky,
                        const AddressList *address_list,
                        unsigned timeout,
                        const ConnectSocketHandler *handler, void *ctx,
                        struct async_operation_ref *async_ref);

#endif
