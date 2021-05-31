/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "Socket.hxx"
#include "event/SocketEvent.hxx"

#include <exception>
#include <utility>

/**
 * Handler for #WasIdleConnection.
 */
class WasIdleConnectionHandler {
public:
	virtual void OnWasIdleConnectionClean() noexcept = 0;
	virtual void OnWasIdleConnectionError(std::exception_ptr e) noexcept = 0;
};

/**
 * Manages a WAS connection which does not currently handle a request.
 * It may be in the progress of "stopping", waiting for the peer's
 * PREMATURE confirmation.
 */
class WasIdleConnection {
	WasSocket socket;

	SocketEvent event;

	WasIdleConnectionHandler &handler;

	/**
	 * The number of bytes received before #WAS_COMMAND_STOP was sent.
	 */
	uint64_t input_received;

	/**
	 * If true, then we're waiting for PREMATURE (after the #WasClient
	 * has sent #WAS_COMMAND_STOP).
	 */
	bool stopping = false;

public:
	WasIdleConnection(EventLoop &event_loop,
			  WasIdleConnectionHandler &_handler) noexcept;

	auto &GetEventLoop() const noexcept {
		return event.GetEventLoop();
	}

	void Open(WasSocket &&_socket) noexcept {
		socket = std::move(_socket);
		event.Open(socket.control);
	}

	const auto &GetSocket() const noexcept {
		return socket;
	}

	void Stop(uint64_t _received) noexcept {
		assert(!stopping);

		stopping = true;
		input_received = _received;
	}

	bool IsStopping() const noexcept {
		return stopping;
	}

	bool Borrow() noexcept {
		if (stopping)
			/* we havn't yet recovered from #WAS_COMMAND_STOP - give
			   up this child process */
			// TODO: improve recovery for this case
			return false;

		event.Cancel();
		return true;
	}

	void Release() noexcept {
		event.ScheduleRead();
	}

private:
	enum class ReceiveResult {
		SUCCESS, AGAIN,
	};

	/**
	 * Receive data on the control channel.
	 *
	 * Throws on error.
	 */
	ReceiveResult ReceiveControl(void *p, size_t size);

	/**
	 * Receive and discard data on the control channel.
	 *
	 * Throws on error.
	 */
	void DiscardControl(size_t size);

	/**
	 * Discard the given amount of data from the input pipe.
	 *
	 * Throws on error.
	 */
	void DiscardInput(uint64_t remaining);

	/**
	 * Attempt to recover after the WAS client sent STOP to the
	 * application.  This method waits for PREMATURE and discards
	 * excess data from the pipe.
	 *
	 * Throws on error.
	 */
	void RecoverStop();

	void OnSocket(unsigned events) noexcept;
};
