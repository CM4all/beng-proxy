/*
 * memcached (binary) protocol specific declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_PROTOCOL_H
#define MEMCACHED_PROTOCOL_H

#include <stdint.h>

enum memcached_magic {
    MEMCACHED_MAGIC_REQUEST = 0x80,
    MEMCACHED_MAGIC_RESPONSE = 0x81,
};

enum memcached_opcode {
    MEMCACHED_OPCODE_GET = 0x00,
    MEMCACHED_OPCODE_SET = 0x01,
    MEMCACHED_OPCODE_ADD = 0x02,
    MEMCACHED_OPCODE_REPLACE = 0x03,
    MEMCACHED_OPCODE_DELETE = 0x04,
    MEMCACHED_OPCODE_INCREMENT = 0x05,
    MEMCACHED_OPCODE_DECREMENT = 0x06,
    MEMCACHED_OPCODE_QUIT = 0x07,
    MEMCACHED_OPCODE_FLUSH = 0x08,
    MEMCACHED_OPCODE_APPEND = 0x0e,
    MEMCACHED_OPCODE_PREPEND = 0x0f,
    MEMCACHED_OPCODE_STAT = 0x10,
};

enum memcached_response_status {
    MEMCACHED_STATUS_NO_ERROR = 0x0000,
    MEMCACHED_STATUS_UNKNOWN_COMMAND = 0x0081,
    MEMCACHED_STATUS_KEY_NOT_FOUND = 0x0001,
    MEMCACHED_STATUS_KEY_EXISTS = 0x0002,
};

struct memcached_request_header {
    uint8_t magic;
    uint8_t opcode;
    uint16_t key_length;
    uint8_t extras_length;
    uint8_t data_type;
    uint16_t reserved;
    uint32_t body_length;
    uint32_t message_id;
    uint8_t cas[8];
};

struct memcached_response_header {
    uint8_t magic;
    uint8_t opcode;
    uint16_t key_length;
    uint8_t extras_length;
    uint8_t data_type;
    uint16_t status;
    uint32_t body_length;
    uint32_t message_id;
    uint8_t cas[8];
};

struct memcached_set_extras {
    uint32_t flags;
    uint32_t expiration;
};

#endif
