/*
 * Serializing memcached request packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_PACKET_H
#define MEMCACHED_PACKET_H

#include "istream.h"
#include "memcached-protocol.h"

istream_t
memcached_request_packet(pool_t pool, enum memcached_opcode opcode,
                         const void *extras, size_t extras_length,
                         const void *key, size_t key_length,
                         istream_t value,
                         uint32_t message_id);

#endif
