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

#include "fcgi_server.hxx"
#include "pool/pool.hxx"
#include "strmap.hxx"
#include "tio.hxx"
#include "util/CharUtil.hxx"

#include <stdio.h>

void
read_fcgi_header(struct fcgi_record_header *header)
{
    read_full(header, sizeof(*header));
    if (header->version != FCGI_VERSION_1)
        abort();
}

static void
read_fcgi_begin_request(struct fcgi_begin_request *begin, uint16_t *request_id)
{
    struct fcgi_record_header header;
    read_fcgi_header(&header);

    if (header.type != FCGI_BEGIN_REQUEST ||
        FromBE16(header.content_length) != sizeof(*begin))
        abort();

    *request_id = header.request_id;

    read_full(begin, sizeof(*begin));
    discard(header.padding_length);
}

static size_t
read_fcgi_length(size_t *remaining_r)
{
    uint8_t a = read_byte(remaining_r);
    if (a < 0x80)
        return a;

    uint8_t b = read_byte(remaining_r), c = read_byte(remaining_r),
        d = read_byte(remaining_r);

    return ((a & 0x7f) << 24) | (b << 16) | (c << 8) | d;
}

static void
handle_fcgi_param(struct pool *pool, FcgiRequest *r,
                  const char *name, const char *value)
{
    if (strcmp(name, "REQUEST_METHOD") == 0) {
        if (strcmp(value, "HEAD") == 0)
            r->method = HTTP_METHOD_HEAD;
        else if (strcmp(value, "POST") == 0)
            r->method = HTTP_METHOD_POST;
    } else if (strcmp(name, "REQUEST_URI") == 0) {
        r->uri = p_strdup(pool, value);
    } else if (memcmp(name, "HTTP_", 5) == 0 && name[5] != 0) {
        char *p = p_strdup(pool, name + 5);

        for (char *q = p; *q != 0; ++q) {
            if (*q == '_')
                *q = '-';
            else if (IsUpperAlphaASCII(*q))
                *q += 'a' - 'A';
        }

        r->headers->Add(*pool, p, p_strdup(pool, value));
    }
}

static void
read_fcgi_params(struct pool *pool, FcgiRequest *r)
{
    r->method = HTTP_METHOD_GET;
    r->uri = nullptr;
    r->headers = strmap_new(pool);

    char name[1024], value[8192];
    while (true) {
        struct fcgi_record_header header;
        read_fcgi_header(&header);

        if (header.type != FCGI_PARAMS ||
            header.request_id != r->id)
            abort();

        size_t remaining = FromBE16(header.content_length);
        if (remaining == 0)
            break;

        while (remaining > 0) {
            size_t name_length = read_fcgi_length(&remaining),
                value_length = read_fcgi_length(&remaining);

            if (name_length >= sizeof(name) || value_length >= sizeof(value) ||
                name_length + value_length > remaining)
                abort();

            read_full(name, name_length);
            name[name_length] = 0;
            remaining -= name_length;

            read_full(value, value_length);
            value[value_length] = 0;
            remaining -= value_length;

            handle_fcgi_param(pool,r, name, value);
        }

        discard(header.padding_length);
    }
}

void
read_fcgi_request(struct pool *pool, FcgiRequest *r)
{
    struct fcgi_begin_request begin;
    read_fcgi_begin_request(&begin, &r->id);
    if (FromBE16(begin.role) != FCGI_RESPONDER)
        abort();

    read_fcgi_params(pool, r);

    const char *content_length = r->headers->Remove("content-length");
    r->length = content_length != nullptr
        ? strtol(content_length, nullptr, 10)
        : -1;

    if (content_length == nullptr) {
        struct fcgi_record_header header;
        ssize_t nbytes = recv(0, &header, sizeof(header),
                              MSG_DONTWAIT|MSG_PEEK);
        if (nbytes == (ssize_t)sizeof(header) &&
            header.version == FCGI_VERSION_1 &&
            header.type == FCGI_STDIN &&
            header.content_length == 0)
            r->length = 0;
    }
}

void
discard_fcgi_request_body(FcgiRequest *r)
{
    struct fcgi_record_header header;

    while (true) {
        read_fcgi_header(&header);

        if (header.type != FCGI_STDIN ||
            header.request_id != r->id)
            abort();

        size_t length = FromBE16(header.content_length);
        if (length == 0)
            break;

        discard(length);
    }
}

void
write_fcgi_stdout(const FcgiRequest *r,
                  const void *data, size_t length)
{
    const struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_STDOUT,
        .request_id = r->id,
        .content_length = ToBE16(length),
        .padding_length = 0,
        .reserved = 0,
    };

    write_full(&header, sizeof(header));
    write_full(data, length);
}

void
write_fcgi_headers(const FcgiRequest *r, http_status_t status,
                   StringMap *headers)
{
    char buffer[8192], *p = buffer;
    p += sprintf(p, "status: %u\n", status);

    if (headers != nullptr)
        for (const auto &i : *headers)
            p += sprintf(p, "%s: %s\n", i.key, i.value);

    p += sprintf(p, "\n");

    write_fcgi_stdout(r, buffer, p - buffer);
}

void
write_fcgi_end(const FcgiRequest *r)
{
    const struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_END_REQUEST,
        .request_id = r->id,
        .content_length = 0,
        .padding_length = 0,
        .reserved = 0,
    };

    write_full(&header, sizeof(header));
}
