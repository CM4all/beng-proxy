/*
 * Serializing memcached request packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached-packet.h"

#include <glib.h>
#include <string.h>

istream_t
memcached_request_packet(pool_t pool, enum memcached_opcode opcode,
                         const void *extras, size_t extras_length,
                         const void *key, size_t key_length,
                         istream_t value,
                         uint32_t message_id)
{
    struct memcached_request_header *header;
    istream_t header_stream, extras_stream;
    off_t value_length;

    value_length = value != NULL ? istream_available(value, false) : 0;
    if (value_length == -1 || value_length >= 0x10000000)
        return NULL;

    header = p_malloc(pool, sizeof(*header));
    header->magic = MEMCACHED_MAGIC_REQUEST;
    header->opcode = opcode;
    header->key_length = g_htons(key_length);
    header->extras_length = extras_length;
    header->data_type = 0;
    header->reserved = 0;
    header->body_length =
        g_htonl(extras_length + key_length + value_length);
    header->message_id = message_id;
    memset(header->cas, 0, sizeof(header->cas));

    header_stream = istream_memory_new(pool, header, sizeof(*header));
    extras_stream = extras_length > 0
        ? istream_memory_new(pool, extras, extras_length)
        : istream_null_new(pool);

    return istream_cat_new(pool, header_stream, extras_stream,
                           key_length == 0 ? istream_null_new(pool)
                           : istream_memory_new(pool, key, key_length),
                           value, NULL);
}
