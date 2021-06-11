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

#include "event/net/BufferedSocket.hxx"
#include "DefaultFifoBuffer.hxx"

#include <was/protocol.h>

#include <string_view>

template<typename T> struct ConstBuffer;

namespace Was {

class ControlHandler {
public:
	/**
	 * A packet was received.
	 *
	 * @return false if the object was closed
	 */
	virtual bool OnWasControlPacket(enum was_command cmd,
					ConstBuffer<void> payload) noexcept = 0;

	/**
	 * Called after a group of control packets have been handled, and
	 * the input buffer is drained.
	 *
	 * @return false if the #WasControl object has been destructed
	 */
	virtual bool OnWasControlDrained() noexcept {
		return true;
	}

	virtual void OnWasControlDone() noexcept = 0;
	virtual void OnWasControlError(std::exception_ptr ep) noexcept = 0;
};

/**
 * Web Application Socket protocol, control channel library.
 */
class Control final : BufferedSocketHandler {
	BufferedSocket socket;

	bool done = false;

	ControlHandler &handler;

	struct {
		unsigned bulk = 0;
	} output;

	DefaultFifoBuffer output_buffer;

public:
	Control(EventLoop &event_loop, SocketDescriptor _fd,
		ControlHandler &_handler) noexcept;

	auto &GetEventLoop() const noexcept {
		return socket.GetEventLoop();
	}

	bool IsDefined() const noexcept {
		return socket.IsValid();
	}

	bool Send(enum was_command cmd,
		  const void *payload, size_t payload_length) noexcept;

	bool SendEmpty(enum was_command cmd) noexcept {
		return Send(cmd, nullptr, 0);
	}

	bool SendString(enum was_command cmd,
			std::string_view payload) noexcept;

	/**
	 * Send a name-value pair (e.g. for #WAS_COMMAND_HEADER and
	 * #WAS_COMMAND_PARAMETER).
	 */
	bool SendPair(enum was_command cmd, std::string_view name,
		      std::string_view value) noexcept;

	bool SendUint64(enum was_command cmd, uint64_t payload) noexcept {
		return Send(cmd, &payload, sizeof(payload));
	}

	bool SendArray(enum was_command cmd,
		       ConstBuffer<const char *> values) noexcept;

	/**
	 * Enables bulk mode.
	 */
	void BulkOn() noexcept {
		++output.bulk;
	}

	/**
	 * Disables bulk mode and flushes the output buffer.
	 */
	bool BulkOff() noexcept;

	void Done() noexcept;

	bool empty() const {
		return socket.IsEmpty() && output_buffer.empty();
	}

private:
	void *Start(enum was_command cmd, size_t payload_length) noexcept;
	bool Finish(size_t payload_length) noexcept;

	void ScheduleRead() noexcept;
	void ScheduleWrite() noexcept;

public:
	/**
	 * Release the socket held by this object.
	 */
	void ReleaseSocket() noexcept;

private:
	void InvokeDone() noexcept {
		ReleaseSocket();
		handler.OnWasControlDone();
	}

	void InvokeError(std::exception_ptr ep) noexcept {
		ReleaseSocket();
		handler.OnWasControlError(ep);
	}

	void InvokeError(const char *msg) noexcept;

	bool InvokeDrained() noexcept {
		return handler.OnWasControlDrained();
	}

	bool TryWrite() noexcept;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	bool OnBufferedDrained() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};

} // namespace Was
