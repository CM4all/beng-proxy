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

#include "Control.hxx"
#include "Error.hxx"
#include "system/Error.hxx"
#include "util/ConstBuffer.hxx"

#include <was/protocol.h>

#include <string.h>

#include <stdio.h>
#include <unistd.h>

namespace Was {

static constexpr auto was_control_timeout = std::chrono::minutes(2);

Control::Control(EventLoop &event_loop, SocketDescriptor _fd,
		 ControlHandler &_handler) noexcept
	:socket(event_loop), handler(_handler)
{
	socket.Init(_fd, FD_SOCKET,
		    was_control_timeout, was_control_timeout,
		    *this);

	socket.ScheduleReadNoTimeout(true);
}

void
Control::ScheduleRead() noexcept
{
	socket.ScheduleReadTimeout(true,
				   socket.IsEmpty()
				   ? Event::Duration(-1)
				   : was_control_timeout);
}

void
Control::ScheduleWrite() noexcept
{
	socket.ScheduleWrite();
}

void
Control::ReleaseSocket() noexcept
{
	assert(socket.IsConnected());

	output_buffer.FreeIfDefined();

	socket.Abandon();
	socket.Destroy();
}

void
Control::InvokeError(const char *msg) noexcept
{
	InvokeError(std::make_exception_ptr(WasProtocolError(msg)));
}

BufferedResult
Control::OnBufferedData()
{
	if (done) {
		InvokeError("received too much control data");
		return BufferedResult::CLOSED;
	}

	while (true) {
		auto r = socket.ReadBuffer();
		const auto header = (const struct was_header *)r.data;
		if (r.size < sizeof(*header) || r.size < sizeof(*header) + header->length) {
			/* not enough data yet */
			if (!InvokeDrained())
				return BufferedResult::CLOSED;

			return BufferedResult::MORE;
		}

		const void *payload = header + 1;

		socket.KeepConsumed(sizeof(*header) + header->length);

		bool success = handler.OnWasControlPacket(was_command(header->command),
							  {payload, header->length});
		if (!success)
			return BufferedResult::CLOSED;
	}
}

bool
Control::OnBufferedClosed() noexcept
{
	InvokeError("WAS control socket closed by peer");
	return false;
}

bool
Control::OnBufferedWrite()
{
	auto r = output_buffer.Read();
	assert(!r.empty());

	ssize_t nbytes = socket.Write(r.data, r.size);
	if (nbytes <= 0) {
		if (nbytes == WRITE_ERRNO)
			throw MakeErrno("WAS control send error");
		return true;
	}

	output_buffer.Consume(nbytes);

	if (output_buffer.empty()) {
		output_buffer.Free();
		socket.UnscheduleWrite();

		if (done) {
			InvokeDone();
			return false;
		}
	}

	return true;
}

bool
Control::OnBufferedDrained() noexcept
{
	return handler.OnWasControlDrained();
}

void
Control::OnBufferedError(std::exception_ptr e) noexcept
{
	InvokeError(e);
}

bool
Control::TryWrite() noexcept
{
	if (output_buffer.empty())
		return true;

	try {
		if (!OnBufferedWrite())
			return false;
	} catch (...) {
		InvokeError(std::current_exception());
		return false;
	}

	if (!output_buffer.empty())
		ScheduleWrite();

	return true;
}

/*
 * constructor
 *
 */

void *
Control::Start(enum was_command cmd, size_t payload_length) noexcept
{
	assert(!done);

	output_buffer.AllocateIfNull();
	auto w = output_buffer.Write().ToVoid();
	struct was_header *header = (struct was_header *)w.data;
	if (w.size < sizeof(*header) + payload_length) {
		InvokeError("control output is too large");
		return nullptr;
	}

	header->command = cmd;
	header->length = payload_length;

	return header + 1;
}

bool
Control::Finish(size_t payload_length) noexcept
{
	assert(!done);

	output_buffer.Append(sizeof(struct was_header) + payload_length);

	return output.bulk > 0 || TryWrite();
}

bool
Control::Send(enum was_command cmd,
	      const void *payload, size_t payload_length) noexcept
{
	assert(!done);

	void *dest = Start(cmd, payload_length);
	if (dest == nullptr)
		return false;

	memcpy(dest, payload, payload_length);
	return Finish(payload_length);
}

bool
Control::SendString(enum was_command cmd, std::string_view payload) noexcept
{
	return Send(cmd, payload.data(), payload.size());
}

bool
Control::SendPair(enum was_command cmd, std::string_view name,
		  std::string_view value) noexcept
{
	const std::size_t payload_size = name.size() + 1 + value.size();

	char *dest = (char *)Start(cmd, payload_size);
	if (dest == nullptr)
		return false;

	dest = std::copy(name.begin(), name.end(), dest);
	*dest++ = '=';
	dest = std::copy(value.begin(), value.end(), dest);

	return Finish(payload_size);
}

bool
Control::SendArray(enum was_command cmd,
		   ConstBuffer<const char *> values) noexcept
{
	for (auto value : values) {
		assert(value != nullptr);

		if (!SendString(cmd, value))
			return false;
	}

	return true;
}

bool
Control::BulkOff() noexcept
{
	assert(output.bulk > 0);

	--output.bulk;
	return output.bulk > 0 || TryWrite();
}

void
Control::Done() noexcept
{
	assert(!done);

	done = true;

	if (!socket.IsEmpty()) {
		InvokeError("received too much control data");
		return;
	}

	if (output_buffer.empty())
		InvokeDone();
}

} // namespace Was
