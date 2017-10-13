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

#include "control_server.hxx"
#include "net/SocketConfig.hxx"
#include "net/SocketAddress.hxx"
#include "util/ByteOrder.hxx"
#include "util/RuntimeError.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <alloca.h>

ControlServer::ControlServer(EventLoop &event_loop, ControlHandler &_handler,
                             const SocketConfig &config)
    :handler(_handler),
     udp(event_loop, config.Create(SOCK_DGRAM), *this)
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

    if (length < sizeof(*magic) || FromBE32(*magic) != control_magic) {
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
        const struct beng_control_header *header =
            (const struct beng_control_header *)data;
        if (length < sizeof(*header)) {
            handler.OnControlError(std::make_exception_ptr(FormatRuntimeError("partial header (length=%zu)",
                                                                              length)));
            return;
        }

        size_t payload_length = FromBE16(header->length);
        beng_control_command command = (beng_control_command)
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
                                payload_length > 0 ? payload : nullptr,
                                payload_length,
                                address);

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = payload + payload_length;
        length -= payload_length;
    }
}

void
ControlServer::OnUdpDatagram(const void *data, size_t length,
                             SocketAddress address, int uid)
{
    if (!handler.OnControlRaw(data, length, address, uid))
        /* discard datagram if raw() returns false */
        return;

    control_server_decode(*this, data, length, address, handler);
}

void
ControlServer::OnUdpError(std::exception_ptr ep)
{
    handler.OnControlError(ep);
}

void
ControlServer::Reply(SocketAddress address,
                     enum beng_control_command command,
                     const void *payload, size_t payload_length)
{
    // TODO: use sendmsg() with iovec[2] instead of assembling a new buffer
    struct beng_control_header *header = (struct beng_control_header *)
        alloca(sizeof(*header) + payload_length);
    if (header == nullptr)
        throw std::runtime_error("alloca() failed");

    header->length = ToBE16(payload_length);
    header->command = ToBE16(command);
    memcpy(header + 1, payload, payload_length);

    udp.Reply(address, header, sizeof(*header) + payload_length);
}
