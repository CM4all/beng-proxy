/*
 * Load balancer for struct uri_with_address.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_BALANCER_H
#define BENG_BALANCER_H

#include "pool.h"

#include <sys/socket.h>

struct balancer;
struct uri_with_address;
struct sockaddr;

struct balancer *
balancer_new(pool_t pool);

void
balancer_free(struct balancer *balancer);

/**
 * Gets the next socket address to connect to.  These are selected in
 * a round-robin fashion, which results in symmetric load-balancing.
 * If a server is known to be faulty, it is not used (see failure.h).
 */
const struct sockaddr *
balancer_get(struct balancer *balancer,
             const struct uri_with_address *uwa, socklen_t *address_size_r);

#endif
