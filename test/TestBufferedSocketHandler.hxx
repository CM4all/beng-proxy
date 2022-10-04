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

#include "event/net/BufferedSocket.hxx"
#include "system/Error.hxx"
#include "util/SpanCast.hxx"

#include <cassert>
#include <optional>
#include <string>

/**
 * A #BufferedSocketHandler for use in unit tests.
 */
template<class Socket>
class TestBufferedSocketHandler : public BufferedSocketHandler {
	EventLoop &event_loop;

	Socket &socket;

	std::string input, output;

	std::exception_ptr error;

	bool block_data = false;

	bool break_data = false, break_remaining = false;

	std::optional<std::size_t> remaining;

public:
	TestBufferedSocketHandler(EventLoop &_event_loop, Socket &_socket) noexcept
		:event_loop(_event_loop), socket(_socket) {}

	explicit TestBufferedSocketHandler(Socket &_socket) noexcept
		:TestBufferedSocketHandler(_socket.GetEventLoop(), _socket) {}

	auto &GetEventLoop() const noexcept {
		return event_loop;
	}

	void BlockData(bool block) noexcept {
		block_data = block;
	}

	void BreakData() noexcept {
		break_data = true;
	}

	void BreakRemaining() noexcept {
		break_remaining = true;
	}

	std::string Read() noexcept {
		return std::exchange(input, std::string{});
	}

	std::string WaitRead() noexcept {
		if (input.empty()) {
			socket.Read();
			if (input.empty()) {
				BreakData();
				socket.ScheduleRead();
				GetEventLoop().Dispatch();
			}
		}

		return Read();
	}

	std::size_t WaitRemaining() noexcept {
		if (!remaining) {
			BreakRemaining();
			socket.ScheduleRead();
			GetEventLoop().Dispatch();
		}

		assert(remaining);

		return *remaining;
	}

	void Write(std::string_view src) noexcept {
		output.append(src);
		socket.ScheduleWrite();
	}

	void Write(std::span<const std::byte> src) noexcept {
		Write(ToStringView(src));
	}

private:
	void DoBreak() noexcept {
		GetEventLoop().Break();
	}

	void DoBreakData() noexcept {
		if (break_data)
			DoBreak();
	}

	void DoBreakRemaining() noexcept {
		if (break_remaining)
			DoBreak();
	}

	void DoBreakEnd() noexcept {
		if (break_data || break_remaining)
			DoBreak();
	}

	void DoBreakError() noexcept {
		if (break_data || break_remaining)
			DoBreak();
	}

public:
	/* virtual methods from BufferedSocketHandler */
	BufferedResult OnBufferedData() override {
		if (block_data)
			return BufferedResult::OK;

		auto r = socket.ReadBuffer();
		assert(!r.empty());

		input.append((const char *)r.data(), r.size());
		socket.DisposeConsumed(r.size());
		DoBreakData();

		return BufferedResult::OK;
	}

	bool OnBufferedClosed() noexcept override {
		socket.Close();
		return true;
	}

	bool OnBufferedRemaining(std::size_t _remaining) noexcept override {
		remaining = _remaining;
		DoBreakRemaining();
		return true;
	}

	bool OnBufferedEnd() noexcept override {
		DoBreakEnd();
		return false;
	}

	bool OnBufferedWrite() override {
		if (output.empty()) {
			socket.UnscheduleWrite();
			return true;
		}

		auto nbytes = socket.Write(AsBytes(std::string_view{output}));
		if (nbytes >= 0) [[likely]] {
			output.erase(0, nbytes);

			if (!output.empty())
				socket.ScheduleWrite();
			return true;
		}

		switch (nbytes) {
		case WRITE_ERRNO:
			break;

		case WRITE_BLOCKING:
			return true;

		case WRITE_DESTROYED:
			DoBreakError();
			return false;

		case WRITE_BROKEN:
			return true;
		}

		throw MakeErrno("Send failed");
	}

	void OnBufferedError(std::exception_ptr e) noexcept override {
		assert(!error);
		error = std::move(e);

		socket.Close();
		DoBreakError();
	}
};
