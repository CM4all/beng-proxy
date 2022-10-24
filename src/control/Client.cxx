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

#include "Client.hxx"
#include "Padding.hxx"
#include "net/RConnectSocket.hxx"
#include "net/SendMessage.hxx"
#include "net/ScmRightsBuilder.hxx"
#include "net/MsgHdr.hxx"
#include "net/SocketError.hxx"
#include "io/Iovec.hxx"
#include "util/ByteOrder.hxx"

inline
BengControlClient::BengControlClient(UniqueSocketDescriptor _socket) noexcept
	:socket(std::move(_socket)) {}

BengControlClient::BengControlClient(const char *host_and_port)
	:BengControlClient(ResolveConnectDatagramSocket(host_and_port,
							5478)) {}

void
BengControlClient::Send(BengProxy::ControlCommand cmd,
			std::span<const std::byte> payload,
			std::span<const FileDescriptor> fds) const
{
	static constexpr uint32_t magic = ToBE32(BengProxy::control_magic);
	const BengProxy::ControlHeader header{ToBE16(payload.size()), ToBE16(uint16_t(cmd))};

	static constexpr uint8_t padding[3] = {0, 0, 0};

	const struct iovec v[] = {
		MakeIovecT(magic),
		MakeIovecT(header),
		MakeIovec(payload),
		MakeIovec(std::span{padding, BengProxy::ControlPaddingSize(payload.size())}),
	};

	MessageHeader msg{std::span{v}};

	ScmRightsBuilder<1> b(msg);
	for (const auto &i : fds)
		b.push_back(i.Get());
	b.Finish(msg);

	SendMessage(socket, msg, 0);
}

void
BengControlClient::Send(std::span<const std::byte> payload) const
{
	auto nbytes = socket.Write(payload.data(), payload.size());
	if (nbytes < 0)
		throw MakeSocketError("send() failed");

}

std::pair<BengProxy::ControlCommand, std::string>
BengControlClient::Receive() const
{
	int result = socket.WaitReadable(10000);
	if (result < 0)
		throw MakeSocketError("poll() failed");

	if (result == 0)
		throw std::runtime_error("Timeout");

	BengProxy::ControlHeader header;
	char payload[4096];

	struct iovec v[] = {
		MakeIovecT(header),
		MakeIovecT(payload),
	};

	auto msg = MakeMsgHdr(v);

	auto nbytes = recvmsg(socket.Get(), &msg, 0);
	if (nbytes < 0)
		throw MakeSocketError("recvmsg() failed");

	if (size_t(nbytes) < sizeof(header))
		throw std::runtime_error("Short receive");

	size_t payload_length = FromBE16(header.length);
	if (sizeof(header) + payload_length > size_t(nbytes))
		throw std::runtime_error("Truncated datagram");

	return std::make_pair(BengProxy::ControlCommand(FromBE16(header.command)),
			      std::string(payload, payload_length));
}

std::string
BengControlClient::MakeTcacheInvalidate(TranslationCommand cmd,
					std::span<const std::byte> payload) noexcept
{
	TranslationHeader h;
	h.length = ToBE16(payload.size());
	h.command = TranslationCommand(ToBE16(uint16_t(cmd)));

	std::string result;
	result.append((const char *)&h, sizeof(h));
	if (!payload.empty()) {
		result.append((const char *)payload.data(), payload.size());
		result.append(BengProxy::ControlPaddingSize(payload.size()), '\0');
	}

	return result;
}
