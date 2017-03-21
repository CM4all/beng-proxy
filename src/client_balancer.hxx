/*
 * Connect to one of a list of addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLIENT_BALANCER_HXX
#define BENG_PROXY_CLIENT_BALANCER_HXX

#include "StickyHash.hxx"

struct pool;
struct Balancer;
struct AddressList;
class EventLoop;
class ConnectSocketHandler;
class CancellablePointer;
class SocketAddress;

/**
 * Open a connection to any address in the specified address list.
 * This is done in a round-robin fashion, ignoring hosts that are
 * known to be down.
 *
 * @param timeout the connect timeout for each attempt [seconds]
 */
void
client_balancer_connect(EventLoop &event_loop, struct pool &pool,
                        Balancer &balancer,
                        bool ip_transparent,
                        SocketAddress bind_address,
                        sticky_hash_t session_sticky,
                        const AddressList *address_list,
                        unsigned timeout,
                        ConnectSocketHandler &handler,
                        CancellablePointer &cancel_ptr);

#endif
