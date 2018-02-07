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

#include "Client.hxx"
#include "net/log/Datagram.hxx"

#include <assert.h>
#include <sys/socket.h>
#include <errno.h>

using namespace Net::Log;

void
LogClient::AppendString(Attribute attribute, const char *value)
{
    assert(value != nullptr);

    AppendAttribute(attribute, value, strlen(value) + 1);
}

void
LogClient::AppendString(Attribute attribute, StringView value)
{
    // TODO: is this the best way to deal with NULL bytes?
    const char *end = value.Find('\0');
    if (end != nullptr)
        value.size = end - value.data;

    AppendAttribute(attribute, value.data, value.size);

    if (position < sizeof(buffer))
        buffer[position] = 0;
    ++position;
}

bool
LogClient::Commit()
{
    assert(fd.IsDefined());
    assert(position > 0);

    if (position > sizeof(buffer))
        /* datagram is too large */
        return false;

    ssize_t nbytes = send(fd.Get(), buffer, position,
                          MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            /* silently ignore EAGAIN */
            return true;

        logger(1, "Failed to send to logger: ", strerror(errno));
        return false;
    }

    if ((size_t)nbytes != position)
        logger(1, "Short send to logger");

    return true;
}

bool
LogClient::Send(const Datagram &d)
{
    Begin();

    if (d.valid_timestamp)
        AppendU64(Attribute::TIMESTAMP, d.timestamp);

    if (d.remote_host != nullptr)
        AppendString(Attribute::REMOTE_HOST, d.remote_host);

    if (d.host != nullptr)
        AppendString(Attribute::HOST, d.host);

    if (d.site != nullptr)
        AppendString(Attribute::SITE, d.site);

    if (d.forwarded_to != nullptr)
        AppendString(Attribute::FORWARDED_TO, d.forwarded_to);

    if (d.valid_http_method)
        AppendU8(Attribute::HTTP_METHOD, d.http_method);

    if (d.http_uri != nullptr)
        AppendString(Attribute::HTTP_URI, d.http_uri);

    if (d.http_referer != nullptr)
        AppendString(Attribute::HTTP_REFERER, d.http_referer);

    if (d.user_agent != nullptr)
        AppendString(Attribute::USER_AGENT, d.user_agent);

    if (d.message != nullptr)
        AppendString(Attribute::MESSAGE, d.message);

    if (d.valid_http_status)
        AppendU16(Attribute::HTTP_STATUS, d.http_status);

    if (d.valid_length)
        AppendU64(Attribute::LENGTH, d.length);

    if (d.valid_traffic) {
        struct {
            uint64_t received, sent;
        } traffic = {
            .received = ToBE64(d.traffic_received),
            .sent = ToBE64(d.traffic_sent),
        };

        AppendAttribute(Attribute::TRAFFIC, &traffic, sizeof(traffic));
    }

    if (d.valid_duration)
        AppendU64(Attribute::DURATION, d.duration);

    if (d.type != Net::Log::Type::UNSPECIFIED)
        AppendU8(Attribute::TYPE, (uint8_t)d.type);

    return Commit();
}
