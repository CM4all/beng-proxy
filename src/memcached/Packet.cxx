/*
 * Copyright 2007-2019 Content Management AG
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

#include "Packet.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_null.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "util/ByteOrder.hxx"

#include <string.h>

UnusedIstreamPtr
memcached_request_packet(struct pool &pool, enum memcached_opcode opcode,
                         const void *extras, size_t extras_length,
                         const void *key, size_t key_length,
                         UnusedIstreamPtr value,
                         uint32_t message_id)
{
    off_t value_length = value
        ? value.GetAvailable(false)
        : 0;
    if (value_length == -1 || value_length >= 0x10000000)
        return nullptr;

    auto header = NewFromPool<memcached_request_header>(pool);
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

    auto header_stream =
        istream_memory_new(pool, header, sizeof(*header));
    auto extras_stream = extras_length > 0
        ? istream_memory_new(pool, extras, extras_length)
        : nullptr;

    return istream_cat_new(pool, std::move(header_stream),
                           std::move(extras_stream),
                           key_length == 0
                           ? nullptr
                           : istream_memory_new(pool, key, key_length),
                           std::move(value));
}
