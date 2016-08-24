/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef UDP_DISTRIBUTE_HXX
#define UDP_DISTRIBUTE_HXX

#include <stddef.h>

struct UdpDistribute;

UdpDistribute *
udp_distribute_new();

void
udp_distribute_free(UdpDistribute *ud);

void
udp_distribute_clear(UdpDistribute *ud);

int
udp_distribute_add(UdpDistribute *ud);

void
udp_distribute_packet(UdpDistribute *ud,
                      const void *payload, size_t payload_length);

#endif
