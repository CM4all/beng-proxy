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

#pragma once

#include "net/control/Protocol.hxx"
#include "event/net/UdpHandler.hxx"
#include "event/net/UdpListener.hxx"

class SocketAddress;
class UniqueSocketDescriptor;
class EventLoop;
class ControlHandler;
struct SocketConfig;

/**
 * Server side part of the "control" protocol.
 */
class ControlServer final : UdpHandler {
	ControlHandler &handler;

	UdpListener socket;

public:
	ControlServer(EventLoop &event_loop, UniqueSocketDescriptor s,
		      ControlHandler &_handler) noexcept;

	ControlServer(EventLoop &event_loop, ControlHandler &_handler,
		      const SocketConfig &config);

	auto &GetEventLoop() const noexcept {
		return socket.GetEventLoop();
	}

	void Enable() noexcept {
		socket.Enable();
	}

	void Disable() noexcept {
		socket.Disable();
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Reply(SocketAddress address,
		   BengProxy::ControlCommand command,
		   std::span<const std::byte> payload);

private:
	/* virtual methods from class UdpHandler */
	bool OnUdpDatagram(std::span<const std::byte> payload,
			   std::span<UniqueFileDescriptor> fds,
			   SocketAddress address, int uid) override;
	void OnUdpError(std::exception_ptr ep) noexcept override;
};
