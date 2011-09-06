/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UDP_LISTENER_H
#define BENG_PROXY_UDP_LISTENER_H

#include "pool.h"

#include <stddef.h>

struct udp;
struct sockaddr;
struct in_addr;

struct udp_handler {
    void (*datagram)(const void *data, size_t length,
                     const struct sockaddr *addres, size_t address_length,
                     void *ctx);
};

struct udp_listener *
udp_listener_port_new(pool_t pool, const char *host_and_port, int default_port,
                      const struct udp_handler *handler, void *ctx);

void
udp_listener_free(struct udp_listener *udp);

/**
 * Replaces the socket.  The old one is closed, and the new one is now
 * owned by this object.
 */
void
udp_listener_set_fd(struct udp_listener *udp, int fd);

/**
 * Joins the specified multicast group.
 *
 * @return true on success
 */
bool
udp_listener_join4(struct udp_listener *udp, const struct in_addr *group);

#endif
