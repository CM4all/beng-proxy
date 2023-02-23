// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "was/async/Socket.hxx"
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
