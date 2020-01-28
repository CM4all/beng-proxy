/*
 * Copyright 2007-2020 CM4all GmbH
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

/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 */

#include "Protocol.hxx"
#include "net/ScmRightsBuilder.hxx"
#include "io/Iovec.hxx"
#include "util/Compiler.h"

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
delegate_send(const void *data, size_t length)
{
	ssize_t nbytes = send(0, data, length, 0);
	if (nbytes < 0) {
		fprintf(stderr, "send() on delegate socket failed: %s\n",
			strerror(errno));
		return false;
	}

	if ((size_t)nbytes != length) {
		fprintf(stderr, "short send() on delegate socket\n");
		return false;
	}

	return true;
}

static bool
delegate_send_int(DelegateResponseCommand command, int value)
{
	const DelegateIntPacket packet = {
		.header = {
			.length = sizeof(packet) - sizeof(packet.header),
			.command = command,
		},
		.value = value,
	};

	return delegate_send(&packet, sizeof(packet));
}

static bool
delegate_send_fd(DelegateResponseCommand command, int fd)
{
	DelegateResponseHeader header = {
		.length = 0,
		.command = command,
	};
	auto vec = MakeIovecT(header);

	struct msghdr msg = {
		.msg_name = nullptr,
		.msg_namelen = 0,
		.msg_iov = &vec,
		.msg_iovlen = 1,
		.msg_control = nullptr,
		.msg_controllen = 0,
		.msg_flags = 0,
	};

	ScmRightsBuilder<1> srb(msg);
	srb.push_back(fd);
	srb.Finish(msg);

	if (sendmsg(0, &msg, 0) < 0) {
		fprintf(stderr, "failed to send fd: %s\n", strerror(errno));
		return false;
	}

	return true;
}

static bool
delegate_handle_open(const char *payload)
{
	int fd = open(payload, O_RDONLY|O_CLOEXEC|O_NOCTTY);
	if (fd >= 0) {
		bool success = delegate_send_fd(DelegateResponseCommand::FD, fd);
		close(fd);
		return success;
	} else {
		/* error: send error code to client */

		return delegate_send_int(DelegateResponseCommand::ERRNO, errno);
	}
}

static bool
delegate_handle(DelegateRequestCommand command,
		const char *payload, size_t length)
{
	(void)length;

	switch (command) {
	case DelegateRequestCommand::OPEN:
		return delegate_handle_open(payload);
	}

	fprintf(stderr, "unknown command: %d\n", int(command));
	return false;
}

int main(int argc gcc_unused, char **argv gcc_unused)
{
	while (true) {
		DelegateRequestHeader header;
		ssize_t nbytes = recv(0, &header, sizeof(header), 0);
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

		if (!delegate_handle(header.command, payload, length))
			return 2;
	}

	return 0;
}
