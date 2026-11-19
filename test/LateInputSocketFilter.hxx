// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "fs/Factory.hxx"
#include "fs/FilteredSocket.hxx"
#include "fs/SocketFilter.hxx"
#include "event/DeferEvent.hxx"
#include "util/StaticFifoBuffer.hxx"

#include <cassert>

/**
 * A #SocketFilter which withholds all input until the peer has closed
 * the socket, and only then submits it to the handler.  This emulates
 * a filter such as #ThreadSocketFilter (e.g. SSL/TLS) which may still
 * hold buffered data when the socket gets closed; such a filter
 * submits response headers to the handler while
 * FilteredSocket::IsConnected() returns false already.
 */
class LateInputSocketFilter final : public SocketFilter {
	FilteredSocket *socket;

	DeferEvent defer_submit;

	StaticFifoBuffer<std::byte, 16384> input;

	bool connected = true;

	bool postponed_remaining = false, postponed_end = false;

public:
	explicit LateInputSocketFilter(EventLoop &event_loop) noexcept
		:defer_submit(event_loop, BIND_THIS_METHOD(Submit)) {}

private:
	/**
	 * Submit the buffered input to the handler, followed by the
	 * postponed "remaining" and "end" calls.
	 */
	void Submit() noexcept {
		assert(!connected);

		while (!input.empty()) {
			switch (socket->InvokeData()) {
			case BufferedResult::OK:
			case BufferedResult::MORE:
				/* no more data will ever arrive */
				break;

			case BufferedResult::AGAIN:
				continue;

			case BufferedResult::DESTROYED:
				return;
			}

			break;
		}

		if (postponed_remaining) {
			postponed_remaining = false;

			if (!socket->InvokeRemaining(input.GetAvailable()))
				return;
		}

		if (postponed_end && input.empty()) {
			postponed_end = false;
			socket->InvokeEnd();
		}
	}

public:
	/* virtual methods from SocketFilter */
	void Init(FilteredSocket &_socket) noexcept override {
		socket = &_socket;
	}

	BufferedResult OnData() noexcept override {
		assert(connected);

		const auto src = socket->InternalReadBuffer();
		const std::size_t nbytes = input.MoveFrom(src);

		/* the test data must fit into our buffer, or else this
		   filter would stall */
		assert(nbytes == src.size());

		socket->InternalConsumed(nbytes);

		return BufferedResult::OK;
	}

	bool IsEmpty() const noexcept override {
		return input.empty();
	}

	bool IsFull() const noexcept override {
		return input.IsFull();
	}

	std::size_t GetAvailable() const noexcept override {
		return input.GetAvailable();
	}

	std::span<std::byte> ReadBuffer() noexcept override {
		return input.Read();
	}

	void Consumed(std::size_t nbytes) noexcept override {
		input.Consume(nbytes);
	}

	void AfterConsumed() noexcept override {}

	BufferedReadResult Read() noexcept override {
		if (!connected)
			return BufferedReadResult::DISCONNECTED;

		return socket->InternalRead();
	}

	ssize_t Write(std::span<const std::byte> src) noexcept override {
		return socket->InternalWrite(src);
	}

	void ScheduleRead() noexcept override {
		socket->InternalScheduleRead();
	}

	void ScheduleWrite() noexcept override {
		socket->InternalScheduleWrite();
	}

	void UnscheduleWrite() noexcept override {
		socket->InternalUnscheduleWrite();
	}

	bool InternalWrite() noexcept override {
		return socket->InvokeWrite();
	}

	void OnClosed() noexcept override {
		assert(connected);

		connected = false;

		/* deliver the buffered input, but not from inside this
		   call stack */
		defer_submit.Schedule();
	}

	bool OnRemaining(std::size_t remaining) noexcept override {
		assert(!connected);

		if (input.empty())
			return socket->InvokeRemaining(remaining);

		postponed_remaining = true;
		return true;
	}

	void OnEnd() override {
		assert(!postponed_end);

		if (postponed_remaining || !input.empty()) {
			postponed_end = true;
			return;
		}

		socket->InvokeEnd();
	}

	void Close() noexcept override {
		delete this;
	}
};

class LateInputSocketFilterFactory final : public SocketFilterFactory {
	EventLoop &event_loop;

public:
	explicit LateInputSocketFilterFactory(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {}

	SocketFilterPtr CreateFilter() override {
		return SocketFilterPtr{new LateInputSocketFilter(event_loop)};
	}
};
