// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "was/async/Socket.hxx"
#include "was/async/Control.hxx"

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
class WasIdleConnection final : Was::ControlHandler {
	WasSocket socket;

	Was::Control control;

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
			  WasSocket &&_socket,
			  WasIdleConnectionHandler &_handler) noexcept;

#ifdef HAVE_URING
	void EnableUring(Uring::Queue &uring_queue) {
		control.EnableUring(uring_queue);
	}
#endif

	auto &GetEventLoop() const noexcept {
		return control.GetEventLoop();
	}

	const auto &GetSocket() const noexcept {
		return socket;
	}

	auto &GetControl() noexcept {
		return control;
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

		return true;
	}

	void Release() noexcept {
		control.SetHandler(*this);
	}

private:
	enum class ReceiveResult {
		SUCCESS, AGAIN,
	};

	/**
	 * Discard the given amount of data from the input pipe.
	 *
	 * Throws on error.
	 */
	void DiscardInput(uint64_t remaining);

	/**
	 * Attempt to recover after the WAS client sent STOP to the
	 * application.  Handles a PREMATURE packet and discards
	 * excess data from the pipe.
	 *
	 * Throws on error.
	 */
	bool OnPrematureControlPacket(std::span<const std::byte> payload);

	/* virtual methods from class WasControlHandler */
	bool OnWasControlPacket(enum was_command cmd,
				std::span<const std::byte> payload) noexcept override;
	bool OnWasControlDrained() noexcept override;
	void OnWasControlDone() noexcept override;
	void OnWasControlHangup() noexcept override;
	void OnWasControlError(std::exception_ptr error) noexcept override;
};
