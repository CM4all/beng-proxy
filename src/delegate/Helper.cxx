// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 */

#include "Protocol.hxx"
#include "system/Error.hxx"
#include "net/SendMessage.hxx"
#include "net/ScmRightsBuilder.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketError.hxx"
#include "net/SocketProtocolError.hxx"
#include "io/Iovec.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"

#include <stdexcept>

#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <unistd.h>

static void
delegate_send(SocketDescriptor s, std::span<const std::byte> src)
{
	ssize_t nbytes = s.Send(src);
	if (nbytes < 0)
		throw MakeSocketError("send() on delegate socket failed");

	if ((size_t)nbytes != src.size())
		throw std::runtime_error{"short send() on delegate socket"};
}

static void
delegate_send_int(SocketDescriptor s, DelegateResponseCommand command, int value)
{
	const DelegateIntPacket packet{
		{
			sizeof(packet) - sizeof(packet.header),
			command,
		},
		value,
	};

	delegate_send(s, ReferenceAsBytes(packet));
}

static void
delegate_send_fd(SocketDescriptor s, DelegateResponseCommand command, FileDescriptor fd)
{
	const DelegateResponseHeader header{
		0,
		command,
	};
	const std::array vec{MakeIovecT(header)};
	MessageHeader msg{vec};

	ScmRightsBuilder<1> srb(msg);
	srb.push_back(fd.Get());
	srb.Finish(msg);

	SendMessage(s, msg, 0);
}

static void
delegate_handle_open(SocketDescriptor s, const char *payload)
{
	UniqueFileDescriptor fd;
	if (fd.OpenReadOnly(payload)) {
		delegate_send_fd(s, DelegateResponseCommand::FD, fd);
	} else {
		/* error: send error code to client */

		delegate_send_int(s, DelegateResponseCommand::ERRNO, errno);
	}
}

static void
delegate_handle(SocketDescriptor s, DelegateRequestCommand command,
		const char *payload, size_t length)
{
	(void)length;

	switch (command) {
	case DelegateRequestCommand::OPEN:
		delegate_handle_open(s, payload);
		return;
	}

	throw SocketProtocolError{"Unknown delegate command"};
}

static void
ReceiveFull(SocketDescriptor s, std::span<std::byte> dest)
{
	while (!dest.empty()) {
		auto nbytes = s.Receive(dest);
		if (nbytes < 0)
			throw MakeSocketError("Failed to receive");

		if (nbytes == 0)
			throw SocketClosedPrematurelyError{};

		dest = dest.subspan(nbytes);
	}
}

int
main(int, char **) noexcept
try {
	const SocketDescriptor s{STDIN_FILENO};

	while (true) {
		DelegateRequestHeader header;
		ssize_t nbytes = s.Receive(std::as_writable_bytes(std::span{&header, 1}));
		if (nbytes < 0)
			throw MakeSocketError("recv() on delegate socket failed");

		if (nbytes == 0)
			break;

		if ((size_t)nbytes != sizeof(header))
			throw std::runtime_error{"short recv() on delegate socket"};

		char payload[4096];
		if (header.length >= sizeof(payload))
			throw SocketProtocolError{"delegate payload too large"};

		ReceiveFull(s, std::as_writable_bytes(std::span{payload}).first(header.length));

		payload[header.length] = 0;

		delegate_handle(s, header.command, payload, header.length);
	}

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
