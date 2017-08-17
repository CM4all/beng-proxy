/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * memcached (binary) protocol specific declarations.
 */

#ifndef MEMCACHED_PROTOCOL_HXX
#define MEMCACHED_PROTOCOL_HXX

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
    MEMCACHED_STATUS_ITEM_NOT_STORED = 0x0005,
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
