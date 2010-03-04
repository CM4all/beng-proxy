/*
 * Serializing memcached request packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_PACKET_H
#define MEMCACHED_PACKET_H

#include "istream.h"
#include "memcached-protocol.h"

/**
 * Serialize a memcached request packet, and return it as an
 * istream_t.
 *
 * @param pool the memory pool used to allocate the packet
 * @param opcode the opcode of the memcached method
 * @param extras optional extra data for the request
 * @param extras_length the length of the extra data
 * @param key key for the request
 * @param key_length the length of the key
 * @param value an optional request value
 * @param message_id the id of the message
 */
istream_t
memcached_request_packet(pool_t pool, enum memcached_opcode opcode,
                         const void *extras, size_t extras_length,
                         const void *key, size_t key_length,
                         istream_t value,
                         uint32_t message_id);

#endif
