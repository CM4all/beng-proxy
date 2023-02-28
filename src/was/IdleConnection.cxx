// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IdleConnection.hxx"
#include "system/Error.hxx"

#include <was/protocol.h>

#include <sys/socket.h>

WasIdleConnection::WasIdleConnection(EventLoop &event_loop,
				     WasIdleConnectionHandler &_handler) noexcept
	:event(event_loop, BIND_THIS_METHOD(OnSocket)),
	 handler(_handler)
{
}

WasIdleConnection::ReceiveResult
WasIdleConnection::ReceiveControl(void *p, size_t size)
{
	ssize_t nbytes = recv(socket.control.Get(), p, size, MSG_DONTWAIT);
	if (nbytes == (ssize_t)size)
		return ReceiveResult::SUCCESS;

	if (nbytes < 0 && errno == EAGAIN) {
		/* the WAS application didn't send enough data (yet); don't
		   bother waiting for more, just give up on this process */
		return ReceiveResult::AGAIN;
	}

	if (nbytes < 0)
		throw MakeErrno("error on idle WAS control connection");
	else if (nbytes > 0)
		throw std::runtime_error("unexpected data from idle WAS control connection");
	else
		throw std::runtime_error("WAS control socket closed unexpectedly");
}

inline void
WasIdleConnection::DiscardControl(size_t size)
{
	while (size > 0) {
		char buffer[1024];
		ssize_t nbytes = recv(socket.control.Get(), buffer,
				      std::min(size, sizeof(buffer)),
				      MSG_DONTWAIT);
		if (nbytes < 0)
			throw MakeErrno("error on idle WAS control connection");
		else if (nbytes == 0)
			throw std::runtime_error("WAS control socket closed unexpectedly");

		size -= nbytes;
	}
}

inline void
WasIdleConnection::DiscardInput(uint64_t remaining)
{
	while (remaining > 0) {
		uint8_t buffer[16384];
		size_t size = std::min(remaining, uint64_t(sizeof(buffer)));
		ssize_t nbytes = socket.input.Read(buffer, size);
		if (nbytes < 0)
			throw MakeErrno("error on idle WAS input pipe");
		else if (nbytes == 0)
			throw std::runtime_error("WAS input pipe closed unexpectedly");

		remaining -= nbytes;
	}
}

inline void
WasIdleConnection::RecoverStop()
{
	uint64_t premature;

	while (true) {
		struct was_header header;
		switch (ReceiveControl(&header, sizeof(header))) {
		case ReceiveResult::SUCCESS:
			break;

		case ReceiveResult::AGAIN:
			/* wait for more data */
			return;
		}

		switch ((enum was_command)header.command) {
		case WAS_COMMAND_NOP:
			/* ignore */
			continue;

		case WAS_COMMAND_HEADER:
		case WAS_COMMAND_STATUS:
		case WAS_COMMAND_NO_DATA:
		case WAS_COMMAND_DATA:
		case WAS_COMMAND_LENGTH:
		case WAS_COMMAND_STOP:
		case WAS_COMMAND_METRIC:
			/* discard & ignore */
			DiscardControl(header.length);
			continue;

		case WAS_COMMAND_REQUEST:
		case WAS_COMMAND_METHOD:
		case WAS_COMMAND_URI:
		case WAS_COMMAND_SCRIPT_NAME:
		case WAS_COMMAND_PATH_INFO:
		case WAS_COMMAND_QUERY_STRING:
		case WAS_COMMAND_PARAMETER:
		case WAS_COMMAND_REMOTE_HOST:
			throw std::runtime_error("unexpected data from idle WAS control connection");

		case WAS_COMMAND_PREMATURE:
			/* this is what we're waiting for */
			break;
		}

		if (ReceiveControl(&premature, sizeof(premature)) != ReceiveResult::SUCCESS)
			throw std::runtime_error("Missing PREMATURE payload");

		break;
	}

	if (premature < input_received)
		throw std::runtime_error("Bogus PREMATURE payload");

	DiscardInput(premature - input_received);

	stopping = false;
	handler.OnWasIdleConnectionClean();
}

inline void
WasIdleConnection::OnSocket(unsigned events) noexcept
try {
	(void)events; // TODO

	if (stopping) {
		RecoverStop();
		return;
	}

	std::byte buffer;
	ssize_t nbytes = recv(socket.control.Get(), &buffer, sizeof(buffer),
			      MSG_DONTWAIT);
	if (nbytes < 0)
		throw MakeErrno("error on idle WAS control connection");
	else if (nbytes > 0)
		throw std::runtime_error("unexpected data from idle WAS control connection");
	else
		throw std::runtime_error("WAS control socket closed unexpectedly");
} catch (...) {
	handler.OnWasIdleConnectionError(std::current_exception());
}
