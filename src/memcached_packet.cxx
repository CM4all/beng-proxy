/*
 * Serializing memcached request packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached_packet.hxx"
#include "istream.h"
#include "istream_cat.hxx"
#include "istream_memory.hxx"
#include "istream_null.hxx"
#include "pool.hxx"
#include "util/ByteOrder.hxx"

#include <string.h>

struct istream *
memcached_request_packet(struct pool &pool, enum memcached_opcode opcode,
                         const void *extras, size_t extras_length,
                         const void *key, size_t key_length,
                         struct istream *value,
                         uint32_t message_id)
{
    off_t value_length = value != nullptr
        ? istream_available(value, false)
        : 0;
    if (value_length == -1 || value_length >= 0x10000000)
        return nullptr;

    auto header = PoolAlloc<memcached_request_header>(pool);
    header->magic = MEMCACHED_MAGIC_REQUEST;
    header->opcode = opcode;
    header->key_length = ToBE16(key_length);
    header->extras_length = extras_length;
    header->data_type = 0;
    header->reserved = 0;
    header->body_length =
        ToBE32(extras_length + key_length + value_length);
    header->message_id = message_id;
    memset(header->cas, 0, sizeof(header->cas));

    struct istream *header_stream =
        istream_memory_new(&pool, header, sizeof(*header));
    struct istream *extras_stream = extras_length > 0
        ? istream_memory_new(&pool, extras, extras_length)
        : istream_null_new(&pool);

    return istream_cat_new(&pool, header_stream, extras_stream,
                           key_length == 0
                           ? istream_null_new(&pool)
                           : istream_memory_new(&pool, key, key_length),
                           value, nullptr);
}
