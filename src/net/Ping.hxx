/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "event/SocketEvent.hxx"
#include "event/TimerEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/Compiler.h"

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
	UniqueSocketDescriptor fd;

	uint16_t ident;

	SocketEvent event;
	TimerEvent timeout_event;

	PingClientHandler &handler;

public:
	PingClient(EventLoop &event_loop,
		   PingClientHandler &_handler) noexcept;

	void Start(SocketAddress address) noexcept;

	void Cancel() noexcept {
		if (fd.IsDefined()) {
			timeout_event.Cancel();
			event.Cancel();
			fd.Close();
		}
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
gcc_const
bool
ping_available() noexcept;
