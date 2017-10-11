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

#include "Parser.hxx"
#include "Datagram.hxx"
#include "util/ByteOrder.hxx"

#include <beng-proxy/log.h>

#include <string.h>

static const void *
read_uint8(uint8_t *value_r, const void *p, const uint8_t *end)
{
    auto src = (const uint8_t *)p;
    if (src + sizeof(*value_r) > end)
        return nullptr;

    *value_r = *src++;
    return src;
}

static const void *
read_uint16(uint16_t *value_r, const void *p, const uint8_t *end)
{
    if ((const uint8_t *)p + sizeof(*value_r) > end)
        return nullptr;

    auto src = (const uint16_t *)p;
    uint16_t value;
    memcpy(&value, src, sizeof(value));

    *value_r = FromBE16(value);
    return src + 1;
}

static const void *
read_uint64(uint64_t *value_r, const void *p, const uint8_t *end)
{
    if ((const uint8_t *)p + sizeof(*value_r) > end)
        return nullptr;

    auto src = (const uint64_t *)p;
    uint64_t value;
    memcpy(&value, src, sizeof(value));

    *value_r = FromBE64(value);
    return src + 1;
}

static const void *
read_string(const char **value_r, const void *p, const uint8_t *end)
{
    auto q = (const char *)p;

    *value_r = q;

    q += strlen(q) + 1;
    return q > (const char *)end ? nullptr : q;
}

static const void *
ReadStringView(StringView &value_r, const void *p, const uint8_t *end)
{
    const char *value;
    p = read_string(&value, p, end);
    value_r = value;
    return p;
}

static AccessLogDatagram
log_server_apply_attributes(const void *p, const uint8_t *end)
{
    assert(p != nullptr);
    assert(end != nullptr);
    assert((const char *)p < (const char *)end);

    AccessLogDatagram datagram;

    while (true) {
        auto attr_p = (const uint8_t *)p;
        if (attr_p >= end)
            return datagram;

        auto attr = (enum beng_log_attribute)*attr_p++;
        p = attr_p;

        switch (attr) {
            uint8_t u8;
            uint16_t u16;

        case LOG_NULL:
            break;

        case LOG_TIMESTAMP:
            p = read_uint64(&datagram.timestamp, p, end);
            datagram.valid_timestamp = true;
            break;

        case LOG_REMOTE_HOST:
            p = read_string(&datagram.remote_host, p, end);
            break;

        case LOG_FORWARDED_TO:
            p = read_string(&datagram.forwarded_to, p, end);
            break;

        case LOG_HOST:
            p = read_string(&datagram.host, p, end);
            break;

        case LOG_SITE:
            p = read_string(&datagram.site, p, end);
            break;

        case LOG_HTTP_METHOD:
            p = read_uint8(&u8, p, end);
            if (p == nullptr)
                throw AccessLogProtocolError();

            datagram.http_method = http_method_t(u8);
            if (!http_method_is_valid(datagram.http_method))
                throw AccessLogProtocolError();

            datagram.valid_http_method = true;
            break;

        case LOG_HTTP_URI:
            p = read_string(&datagram.http_uri, p, end);
            break;

        case LOG_HTTP_REFERER:
            p = read_string(&datagram.http_referer, p, end);
            break;

        case LOG_USER_AGENT:
            p = read_string(&datagram.user_agent, p, end);
            break;

        case LOG_MESSAGE:
            p = ReadStringView(datagram.message, p, end);
            break;

        case LOG_HTTP_STATUS:
            p = read_uint16(&u16, p, end);
            if (p == nullptr)
                throw AccessLogProtocolError();

            datagram.http_status = http_status_t(u16);
            if (!http_status_is_valid(datagram.http_status))
                throw AccessLogProtocolError();

            datagram.valid_http_status = true;
            break;

        case LOG_LENGTH:
            p = read_uint64(&datagram.length, p, end);
            datagram.valid_length = true;
            break;

        case LOG_TRAFFIC:
            p = read_uint64(&datagram.traffic_received, p, end);
            if (p != nullptr)
                p = read_uint64(&datagram.traffic_sent, p, end);
            datagram.valid_traffic = true;
            break;

        case LOG_DURATION:
            p = read_uint64(&datagram.duration, p, end);
            datagram.valid_duration = true;
            break;
        }

        if (p == nullptr)
            throw AccessLogProtocolError();
    }
}

AccessLogDatagram
log_server_apply_datagram(const void *p, const void *end)
{
    auto magic = (const uint32_t *)p;
    if (*magic != log_magic)
        throw AccessLogProtocolError();

    return log_server_apply_attributes(magic + 1, (const uint8_t *)end);
}
