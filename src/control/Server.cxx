/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "Handler.hxx"
#include "Padding.hxx"
#include "net/SocketConfig.hxx"
#include "net/SocketAddress.hxx"
#include "net/SendMessage.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/Iovec.hxx"
#include "util/ByteOrder.hxx"
#include "util/RuntimeError.hxx"

ControlServer::ControlServer(EventLoop &event_loop, UniqueSocketDescriptor s,
			     ControlHandler &_handler) noexcept
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
		      std::span<UniqueFileDescriptor> fds,
		      SocketAddress address, int uid,
		      ControlHandler &handler)
{
	/* verify the magic number */

	const uint32_t *magic = (const uint32_t *)data;

	if (length < sizeof(*magic) || FromBE32(*magic) != BengProxy::control_magic)
		throw std::runtime_error("wrong magic");

	data = magic + 1;
	length -= sizeof(*magic);

	if (!BengProxy::IsControlSizePadded(length))
		throw FormatRuntimeError("odd control packet (length=%zu)", length);

	/* now decode all commands */

	while (length > 0) {
		const auto *header = (const BengProxy::ControlHeader *)data;
		if (length < sizeof(*header))
			throw FormatRuntimeError("partial header (length=%zu)",
						 length);

		size_t payload_length = FromBE16(header->length);
		const auto command = (BengProxy::ControlCommand)
			FromBE16(header->command);

		data = header + 1;
		length -= sizeof(*header);

		const std::byte *payload = (const std::byte *)data;
		if (length < payload_length)
			throw FormatRuntimeError("partial payload (length=%zu, expected=%zu)",
						 length, payload_length);

		/* this command is ok, pass it to the callback */

		handler.OnControlPacket(control_server, command,
					{payload_length > 0 ? payload : nullptr, payload_length},
					fds,
					address, uid);

		payload_length = BengProxy::PadControlSize(payload_length);

		data = payload + payload_length;
		length -= payload_length;
	}
}

bool
ControlServer::OnUdpDatagram(std::span<const std::byte> payload,
			     std::span<UniqueFileDescriptor> fds,
			     SocketAddress address, int uid)
{
	control_server_decode(*this, payload.data(), payload.size(),
			      fds, address, uid, handler);
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
		     std::span<const std::byte> payload)
{
	const struct BengProxy::ControlHeader header{ToBE16(payload.size()), ToBE16(uint16_t(command))};

	const struct iovec v[] = {
		MakeIovecT(header),
		MakeIovec(payload),
	};

	SendMessage(socket.GetSocket(),
		    MessageHeader{v}.SetAddress(address),
		    MSG_DONTWAIT|MSG_NOSIGNAL);
}
