// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IdleConnection.hxx"
#include "was/async/Socket.hxx"
#include "system/Error.hxx"
#include "net/SocketError.hxx"
#include "net/SocketProtocolError.hxx"
#include "util/Unaligned.hxx"

#include <was/protocol.h>

#include <array>

#include <sys/socket.h>

WasIdleConnection::WasIdleConnection(EventLoop &event_loop,
				     WasSocket &&socket,
				     WasIdleConnectionHandler &_handler) noexcept
	:control(event_loop, std::move(socket.control), *this),
	 input(std::move(socket.input)), output(std::move(socket.output)),
	 handler(_handler)
{
}

inline void
WasIdleConnection::DiscardInput(uint64_t remaining)
{
	while (remaining > 0) {
		std::array<std::byte, 16384> buffer;
		std::span<std::byte> dest = buffer;
		if (dest.size() > remaining)
			dest = dest.first(remaining);
		ssize_t nbytes = input.Read(dest);
		if (nbytes < 0)
			throw MakeErrno("error on idle WAS input pipe");
		else if (nbytes == 0)
			throw SocketClosedPrematurelyError{"WAS input pipe closed unexpectedly"};

		remaining -= nbytes;
	}
}

inline bool
WasIdleConnection::OnPrematureControlPacket(std::span<const std::byte> payload)
{
	uint64_t premature;
	if (payload.size() != sizeof(premature))
		throw SocketProtocolError{"Malformed PREMATURE payload"};

	premature = LoadUnaligned<uint64_t>(payload.data());
	if (premature < input_received)
		throw SocketProtocolError{"Bogus PREMATURE payload"};

	DiscardInput(premature - input_received);

	stopping = false;
	handler.OnWasIdleConnectionClean();
	return true;
}

bool
WasIdleConnection::OnWasControlPacket(enum was_command cmd,
				      std::span<const std::byte> payload) noexcept
try {
	if (stopping) {
		switch (cmd) {
		case WAS_COMMAND_NOP:
			/* ignore */
			return true;

		case WAS_COMMAND_HEADER:
		case WAS_COMMAND_STATUS:
		case WAS_COMMAND_NO_DATA:
		case WAS_COMMAND_DATA:
		case WAS_COMMAND_LENGTH:
		case WAS_COMMAND_STOP:
		case WAS_COMMAND_METRIC:
			/* discard & ignore */
			return true;

		case WAS_COMMAND_REQUEST:
		case WAS_COMMAND_METHOD:
		case WAS_COMMAND_URI:
		case WAS_COMMAND_SCRIPT_NAME:
		case WAS_COMMAND_PATH_INFO:
		case WAS_COMMAND_QUERY_STRING:
		case WAS_COMMAND_PARAMETER:
		case WAS_COMMAND_REMOTE_HOST:
			throw SocketProtocolError{"unexpected data from idle WAS control connection"};

		case WAS_COMMAND_PREMATURE:
			/* this is what we're waiting for */
			return OnPrematureControlPacket(payload);
		}
	}

	switch (cmd) {
	case WAS_COMMAND_NOP:
		/* ignore */
		return true;

	default:
		throw std::runtime_error("unexpected data from idle WAS control connection");
	}
} catch (...) {
	handler.OnWasIdleConnectionError(std::current_exception());
	return false;
}

bool
WasIdleConnection::OnWasControlDrained() noexcept
{
	return true;
}

void
WasIdleConnection::OnWasControlDone() noexcept
{
}

void
WasIdleConnection::OnWasControlHangup() noexcept
{
	handler.OnWasIdleConnectionError(std::make_exception_ptr(SocketClosedPrematurelyError{"WAS control socket closed unexpectedly"}));
}

void
WasIdleConnection::OnWasControlError(std::exception_ptr error) noexcept
{
	handler.OnWasIdleConnectionError(std::move(error));
}
