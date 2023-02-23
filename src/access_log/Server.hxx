// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "net/log/Datagram.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/StaticSocketAddress.hxx"

#include <array>
#include <span>

/**
 * An extension of #Net::Log::Datagram which contains information on
 * the receipt.
 */
struct ReceivedAccessLogDatagram : Net::Log::Datagram {
	SocketAddress logger_client_address;

	/**
	 * The raw datagram payload.
	 */
	std::span<const std::byte> raw;
};

/**
 * A simple server for the logging protocol.
 */
class AccessLogServer {
	const SocketDescriptor fd;

	ReceivedAccessLogDatagram datagram;

	static constexpr size_t N = 32;
	std::array<StaticSocketAddress, N> addresses;
	std::array<std::byte[16384], N> payloads;
	std::array<size_t, N> sizes;
	size_t n_payloads = 0, current_payload = 0;

public:
	explicit AccessLogServer(SocketDescriptor _fd):fd(_fd) {}

	/**
	 * Construct an instance with the default socket (STDIN_FILENO).
	 */
	AccessLogServer();

	AccessLogServer(const AccessLogServer &) = delete;
	AccessLogServer &operator=(const AccessLogServer &) = delete;

	const ReceivedAccessLogDatagram *Receive();

	template<typename F>
	void Run(F &&f) {
		while (const auto *d = Receive())
			f(*d);
	}

private:
	bool Fill();
};
