// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 */

#include "Protocol.hxx"
#include "net/SendMessage.hxx"
#include "net/ScmRightsBuilder.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/Iovec.hxx"
#include "util/PrintException.hxx"

#include <stdbool.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <unistd.h>

static bool
delegate_send(SocketDescriptor s, std::span<const std::byte> src) noexcept
{
	ssize_t nbytes = s.Send(src);
	if (nbytes < 0) {
		fprintf(stderr, "send() on delegate socket failed: %s\n",
			strerror(errno));
		return false;
	}

	if ((size_t)nbytes != src.size()) {
		fprintf(stderr, "short send() on delegate socket\n");
		return false;
	}

	return true;
}

static bool
delegate_send_int(SocketDescriptor s, DelegateResponseCommand command, int value)
{
	const DelegateIntPacket packet{
		{
			sizeof(packet) - sizeof(packet.header),
			command,
		},
		value,
	};

	return delegate_send(s, std::as_bytes(std::span{&packet, 1}));
}

static bool
delegate_send_fd(SocketDescriptor s, DelegateResponseCommand command, int fd)
{
	const DelegateResponseHeader header{
		0,
		command,
	};
	const std::array vec{MakeIovecT(header)};
	MessageHeader msg{vec};

	ScmRightsBuilder<1> srb(msg);
	srb.push_back(fd);
	srb.Finish(msg);

	try {
		SendMessage(s, msg, 0);
	} catch (...) {
		PrintException(std::current_exception());
	}

	return true;
}

static bool
delegate_handle_open(SocketDescriptor s, const char *payload)
{
	int fd = open(payload, O_RDONLY|O_CLOEXEC|O_NOCTTY);
	if (fd >= 0) {
		bool success = delegate_send_fd(s, DelegateResponseCommand::FD, fd);
		close(fd);
		return success;
	} else {
		/* error: send error code to client */

		return delegate_send_int(s, DelegateResponseCommand::ERRNO, errno);
	}
}

static bool
delegate_handle(SocketDescriptor s, DelegateRequestCommand command,
		const char *payload, size_t length)
{
	(void)length;

	switch (command) {
	case DelegateRequestCommand::OPEN:
		return delegate_handle_open(s, payload);
	}

	fprintf(stderr, "unknown command: %d\n", int(command));
	return false;
}

int
main(int, char **) noexcept
{
	const SocketDescriptor s{STDIN_FILENO};

	while (true) {
		DelegateRequestHeader header;
		ssize_t nbytes = s.Receive(std::as_writable_bytes(std::span{&header, 1}));
		if (nbytes < 0) {
			fprintf(stderr, "recv() on delegate socket failed: %s\n",
				strerror(errno));
			return 2;
		}

		if (nbytes == 0)
			break;

		if ((size_t)nbytes != sizeof(header)) {
			fprintf(stderr, "short recv() on delegate socket\n");
			return 2;
		}

		char payload[4096];
		if (header.length >= sizeof(payload)) {
			fprintf(stderr, "delegate payload too large\n");
			return 2;
		}

		size_t length = 0;

		while (length < header.length) {
			nbytes = recv(0, payload + length,
				      sizeof(payload) - 1 - length, 0);
			if (nbytes < 0) {
				fprintf(stderr, "recv() on delegate socket failed: %s\n",
					strerror(errno));
				return 2;
			}

			if (nbytes == 0)
				break;

			length += (size_t)nbytes;
		}

		payload[length] = 0;

		if (!delegate_handle(s, header.command, payload, length))
			return 2;
	}

	return 0;
}
