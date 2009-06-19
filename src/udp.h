/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UDP_H
#define BENG_PROXY_UDP_H

#include "pool.h"

#include <stddef.h>

struct udp;
struct sockaddr;
struct in_addr;

typedef void (*udp_callback_t)(const void *data, size_t length,
                               const struct sockaddr *addr, size_t addrlen,
                               void *ctx);

struct udp *
udp_new(pool_t pool, const char *host_and_port, int default_port,
        udp_callback_t callback, void *ctx);

void
udp_free(struct udp *udp);

/**
 * Joins the specified multicast group.
 *
 * @return true on success
 */
bool
udp_join4(struct udp *udp, const struct in_addr *group);

void
udp_event_add(struct udp *udp);

void
udp_event_del(struct udp *udp);

#endif
