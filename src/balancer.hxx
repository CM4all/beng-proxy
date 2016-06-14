/*
 * Load balancer for AddressList.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BALANCER_HXX
#define BENG_PROXY_BALANCER_HXX

#include <sys/socket.h>

struct pool;
struct Balancer;
struct AddressList;
class EventLoop;
class SocketAddress;

Balancer *
balancer_new(struct pool &pool, EventLoop &event_loop);

void
balancer_free(Balancer *balancer);

/**
 * Gets the next socket address to connect to.  These are selected in
 * a round-robin fashion, which results in symmetric load-balancing.
 * If a server is known to be faulty, it is not used (see failure.h).
 *
 * @param session a portion of the session id used to select an
 * address if stickiness is enabled; 0 if there is no session
 */
SocketAddress
balancer_get(Balancer &balancer, const AddressList &list,
             unsigned session);

void
balancer_event_add(Balancer &balancer);

void
balancer_event_del(Balancer &balancer);

#endif
