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

#include "ajp_server.hxx"
#include "tio.hxx"
#include "pool.hxx"
#include "strmap.hxx"

#include <string.h>
#include <stdlib.h>

static char *
read_string_n(struct pool *pool, size_t length, size_t *remaining_r)
{
    if (length == 0xffff)
        return nullptr;

    if (*remaining_r < length + 1)
        exit(EXIT_FAILURE);

    char *value = (char *)p_malloc(pool, length + 1);
    read_full(value, length + 1);
    if (value[length] != 0)
        exit(EXIT_FAILURE);

    *remaining_r -= length + 1;
    return value;
}

static char *
read_string(struct pool *pool, size_t *remaining_r)
{
    const size_t length = read_short(remaining_r);
    return read_string_n(pool, length, remaining_r);
}

void
read_ajp_header(struct ajp_header *header)
{
    read_full(header, sizeof(*header));
    if (header->a != 0x12 || header->b != 0x34)
        exit(EXIT_FAILURE);
}

static void
write_string(const char *value)
{
    if (value != nullptr) {
        size_t length = strlen(value);
        if (length > 0xfffe)
            length = 0xfffe;

        write_short(length);
        write_full(value, length);
        write_byte(0);
    } else
        write_short(0xffff);
}

static void
write_get_body_chunk(size_t length)
{
    assert(length <= 0xffff);

    static constexpr struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(3),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_GET_BODY_CHUNK);
    write_short(length);
}

void
read_ajp_request(struct pool *pool, struct ajp_request *r)
{
    struct ajp_header header;
    read_ajp_header(&header);

    size_t remaining = FromBE16(header.length);

    r->code = (ajp_code)read_byte(&remaining);
    if (r->code != AJP_CODE_FORWARD_REQUEST) {
        discard(remaining);
        return;
    }

    r->method = (ajp_method)read_byte(&remaining);

    read_string(pool, &remaining); /* protocol */
    r->uri = read_string(pool, &remaining);
    read_string(pool, &remaining); /* remote_address */
    read_string(pool, &remaining); /* remote_host */
    read_string(pool, &remaining); /* server_name */
    read_short(&remaining); /* server_port */
    read_byte(&remaining); /* is_ssl */

    r->headers = strmap_new(pool);

    unsigned n_headers = read_short(&remaining);
    while (n_headers-- > 0) {
        unsigned name_length = read_short(&remaining);
        const ajp_header_code code = (ajp_header_code)name_length;
        const char *name = ajp_decode_header_name(code);
        if (name == nullptr) {
            char *name2 = read_string_n(pool, name_length, &remaining);
            if (name2 == nullptr)
                exit(EXIT_FAILURE);

            name = p_strndup_lower(pool, name2, name_length);
        }

        const char *value = read_string(pool, &remaining);
        r->headers->Add(name, value);
    }

    // ...

    discard(remaining);

    const char *length_string = r->headers->Get("content-length");
    r->length = length_string != nullptr
        ? strtoul(length_string, nullptr, 10)
        : 0;
    r->body = r->length > 0
        ? (uint8_t *)p_malloc(pool, r->length)
        : nullptr;
    r->requested = 0;
    r->received = 0;
}

void
read_ajp_request_body_chunk(struct ajp_request *r)
{
    assert(r->length > 0);
    assert(r->received < r->length);
    assert(r->body != nullptr);

    const size_t remaining = r->length - r->received;

    while (r->requested <= r->received) {
        size_t nbytes = remaining;
        if (nbytes > 8192)
            nbytes = 8192;

        write_get_body_chunk(nbytes);
        r->requested += nbytes;
    }

    struct ajp_header header;
    read_ajp_header(&header);

    size_t packet_length = FromBE16(header.length);
    size_t chunk_length = read_short(&packet_length);
    if (chunk_length == 0 || chunk_length > packet_length ||
        chunk_length > remaining)
        exit(EXIT_FAILURE);

    read_full(r->body + r->received, chunk_length);
    r->received += chunk_length;

    size_t junk_length = packet_length - chunk_length;
    discard(junk_length);
}

void
read_ajp_end_request_body_chunk(gcc_unused struct ajp_request *r)
{
    assert(r->length > 0);
    assert(r->received == r->length);
    assert(r->body != nullptr);

    struct ajp_header header;
    read_ajp_header(&header);
    size_t packet_length = FromBE16(header.length);
    if (packet_length == 0)
        return;

    size_t chunk_length = read_short(&packet_length);
    if (chunk_length != 0)
        exit(EXIT_FAILURE);
}

void
discard_ajp_request_body(struct ajp_request *r)
{
    if (r->length == 0)
        return;

    while (r->received < r->length)
        read_ajp_request_body_chunk(r);

    read_ajp_end_request_body_chunk(r);
}

void
write_headers(http_status_t status, const StringMap *headers)
{
    unsigned n = 0;
    size_t length = 7;

    if (headers != nullptr) {
        for (const auto &i : *headers) {
            ++n;
            length += 4;

            enum ajp_response_header_code code =
                ajp_encode_response_header_name(i.key);
            if (code == AJP_RESPONSE_HEADER_NONE)
                length += strlen(i.key) + 1;

            length += strlen(i.value) + 1;
        }
    }

    const struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(length),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_SEND_HEADERS);
    write_short(status);
    write_string(nullptr);

    write_short(n);

    if (headers != nullptr) {
        for (const auto &i : *headers) {
            enum ajp_response_header_code code =
                ajp_encode_response_header_name(i.key);
            if (code == AJP_RESPONSE_HEADER_NONE)
                write_string(i.key);
            else
                write_short(code);

            write_string(i.value);
        }
    }
}

void
write_body_chunk(const void *value, size_t length, size_t junk)
{
    assert(length + junk <= 0xffff);

    const struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(3 + length + junk),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_SEND_BODY_CHUNK);
    write_short(length);
    write_full(value, length);
    fill(junk);
}

void
write_end()
{
    static constexpr struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(1),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_END_RESPONSE);
}
