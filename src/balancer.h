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
struct address_list;
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
const struct address_envelope *
balancer_get(struct balancer *balancer, const struct address_list *list);

#endif
