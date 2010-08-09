/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UDP_DISTRIBUTE_H
#define BENG_PROXY_UDP_DISTRIBUTE_H

#include "pool.h"

#include <stddef.h>

struct udp_distribute *
udp_distribute_new(pool_t pool);

void
udp_distribute_free(struct udp_distribute *ud);

void
udp_distribute_clear(struct udp_distribute *ud);

int
udp_distribute_add(struct udp_distribute *ud);

void
udp_distribute_packet(struct udp_distribute *ud,
                      const void *payload, size_t payload_length);

#endif
