// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Launch a number of logger processes that receive a copy of all log
 * datagrams.
 */

#include "Launch.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

static constexpr unsigned MAX_CHILDREN = 32;

struct Child {
	SocketDescriptor fd;
};

static unsigned n_children;
static Child children[MAX_CHILDREN];

static bool
Forward(SocketDescriptor src)
{
	std::byte buffer[65536];
	ssize_t nbytes = src.Receive(buffer);
	if (nbytes <= 0) {
		if (nbytes < 0) {
			if (errno == EAGAIN || errno == EINTR)
				return true;

			fprintf(stderr, "Failed to receive: %s\n", strerror(errno));
		}

		return false;
	}

	const auto span = std::span{buffer}.first(nbytes);

	for (unsigned i = 0; i < n_children; ++i) {
		Child &c = children[i];
		c.fd.Send(span, MSG_DONTWAIT);
	}

	return true;
}

int
main(int argc, char **argv)
try {
	if (argc < 2 || unsigned(argc) > 1 + MAX_CHILDREN) {
		fprintf(stderr, "Usage: %s PROGRAM1 PROGRAM2 ...\n", argv[0]);
		return EXIT_FAILURE;
	}

	const SocketDescriptor src{STDIN_FILENO};

	for (int i = 1; i < argc; ++i) {
		const char *program = argv[i];
		auto process = LaunchLogger(program, nullptr);
		Child &child = children[n_children++];
		child.fd = process.fd.Release();
	}

	while (Forward(src)) {}
	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
