// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/SocketEvent.hxx"
#include "event/CoarseTimerEvent.hxx"

#include <exception>

class PingClientHandler {
public:
	virtual void PingResponse() noexcept = 0;
	virtual void PingTimeout() noexcept = 0;
	virtual void PingError(std::exception_ptr ep) noexcept = 0;
};

/**
 * Sends a "ping" (ICMP echo-request) to the server, and waits for the
 * reply.
 */
class PingClient final {
	uint16_t ident;

	SocketEvent event;
	CoarseTimerEvent timeout_event;

	PingClientHandler &handler;

public:
	PingClient(EventLoop &event_loop,
		   PingClientHandler &_handler) noexcept;

	~PingClient() noexcept {
		event.Close();
	}

	void Start(SocketAddress address) noexcept;

	void Cancel() noexcept {
		timeout_event.Cancel();
		event.Close();
	}

private:
	void ScheduleRead() noexcept;
	void EventCallback(unsigned events) noexcept;
	void OnTimeout() noexcept;

	void Read() noexcept;
};

/**
 * Is the "ping" client available?
 */
[[gnu::const]]
bool
ping_available() noexcept;
