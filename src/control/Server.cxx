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

#include "Server.hxx"
#include "net/SocketConfig.hxx"
#include "net/SocketAddress.hxx"
#include "net/SendMessage.hxx"
#include "util/ByteOrder.hxx"
#include "util/ConstBuffer.hxx"
#include "util/RuntimeError.hxx"
#include "util/Macros.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <alloca.h>

ControlServer::ControlServer(EventLoop &event_loop, UniqueSocketDescriptor s,
                             ControlHandler &_handler)
    :handler(_handler), socket(event_loop, std::move(s), *this)
{
}

ControlServer::ControlServer(EventLoop &event_loop, ControlHandler &_handler,
                             const SocketConfig &config)
    :ControlServer(event_loop, config.Create(SOCK_DGRAM), _handler)
{
}

static void
control_server_decode(ControlServer &control_server,
                      const void *data, size_t length,
                      SocketAddress address,
                      ControlHandler &handler)
{
    /* verify the magic number */

    const uint32_t *magic = (const uint32_t *)data;

    if (length < sizeof(*magic) || FromBE32(*magic) != BengProxy::control_magic) {
        handler.OnControlError(std::make_exception_ptr(std::runtime_error("wrong magic")));
        return;
    }

    data = magic + 1;
    length -= sizeof(*magic);

    if (length % 4 != 0) {
        handler.OnControlError(std::make_exception_ptr(FormatRuntimeError("odd control packet (length=%zu)", length)));
        return;
    }

    /* now decode all commands */

    while (length > 0) {
        const auto *header = (const BengProxy::ControlHeader *)data;
        if (length < sizeof(*header)) {
            handler.OnControlError(std::make_exception_ptr(FormatRuntimeError("partial header (length=%zu)",
                                                                              length)));
            return;
        }

        size_t payload_length = FromBE16(header->length);
        const auto command = (BengProxy::ControlCommand)
            FromBE16(header->command);

        data = header + 1;
        length -= sizeof(*header);

        const char *payload = (const char *)data;
        if (length < payload_length) {
            handler.OnControlError(std::make_exception_ptr(FormatRuntimeError("partial payload (length=%zu, expected=%zu)",
                                                                              length, payload_length)));
            return;
        }

        /* this command is ok, pass it to the callback */

        handler.OnControlPacket(control_server, command,
                                {payload_length > 0 ? payload : nullptr, payload_length},
                                address);

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = payload + payload_length;
        length -= payload_length;
    }
}

bool
ControlServer::OnUdpDatagram(const void *data, size_t length,
                             SocketAddress address, int uid)
{
    if (!handler.OnControlRaw({data, length}, address, uid))
        /* discard datagram if raw() returns false */
        return true;

    control_server_decode(*this, data, length, address, handler);
    return true;
}

void
ControlServer::OnUdpError(std::exception_ptr ep) noexcept
{
    handler.OnControlError(ep);
}

void
ControlServer::Reply(SocketAddress address,
                     BengProxy::ControlCommand command,
                     const void *payload, size_t payload_length)
{
    const struct BengProxy::ControlHeader header{ToBE16(payload_length), ToBE16(uint16_t(command))};

    struct iovec v[] = {
        { const_cast<BengProxy::ControlHeader *>(&header), sizeof(header) },
        { const_cast<void *>(payload), payload_length },
    };

    SendMessage(socket.GetSocket(),
                MessageHeader(ConstBuffer<struct iovec>(v, ARRAY_SIZE(v)))
                .SetAddress(address),
                MSG_DONTWAIT|MSG_NOSIGNAL);
}
