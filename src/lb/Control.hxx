// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/net/control/Handler.hxx"
#include "event/net/control/Server.hxx"
#include "io/Logger.hxx"

#include <string_view>

struct LbInstance;
struct LbControlConfig;

class LbControl final : BengControl::Handler {
	const LLogger logger;

	LbInstance &instance;

	BengControl::Server server;

public:
	LbControl(LbInstance &_instance, const LbControlConfig &config);

	auto &GetEventLoop() const noexcept {
		return server.GetEventLoop();
	}

	void Enable() noexcept {
		server.Enable();
	}

	void Disable() noexcept {
		server.Disable();
	}

private:
	void InvalidateTranslationCache(std::span<const std::byte> payload,
					SocketAddress address);

	void EnableNode(const char *payload, size_t length);
	void FadeNode(const char *payload, size_t length);

	/* virtual methods from class BengControl::Handler */
	void OnControlPacket(BengControl::Command command,
			     std::span<const std::byte> payload,
			     std::span<UniqueFileDescriptor> fds,
			     SocketAddress address, int uid) override;

	void OnControlError(std::exception_ptr ep) noexcept override;
};
