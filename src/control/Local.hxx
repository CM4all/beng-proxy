// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/net/control/Handler.hxx"

#include <memory>

class EventLoop;

/**
 * Control server on an implicitly configured local socket.
 */
class LocalControl final : ControlHandler {
	const char *const prefix;

	ControlHandler &handler;

	std::unique_ptr<ControlServer> server;

public:
	LocalControl(const char *_prefix, ControlHandler &_handler)
		:prefix(_prefix), handler(_handler) {}

	void Open(EventLoop &event_loop);

	/* virtual methods from class ControlHandler */
	void OnControlPacket(ControlServer &control_server,
			     BengProxy::ControlCommand command,
			     std::span<const std::byte> payload,
			     std::span<UniqueFileDescriptor> fds,
			     SocketAddress address, int uid) override;

	void OnControlError(std::exception_ptr ep) noexcept override;
};
