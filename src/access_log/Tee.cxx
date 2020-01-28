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
	int fd;
};

static unsigned n_children;
static Child children[MAX_CHILDREN];

static bool
Forward()
{
	char buffer[65536];
	ssize_t nbytes = recv(STDIN_FILENO, buffer, sizeof(buffer), 0);
	if (nbytes <= 0) {
		if (nbytes < 0) {
			if (errno == EAGAIN || errno == EINTR)
				return true;

			fprintf(stderr, "Failed to receive: %s\n", strerror(errno));
		}

		return false;
	}

	for (unsigned i = 0; i < n_children; ++i) {
		Child &c = children[i];
		send(c.fd, buffer, nbytes, MSG_DONTWAIT|MSG_NOSIGNAL);
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

	for (int i = 1; i < argc; ++i) {
		const char *program = argv[i];
		auto process = LaunchLogger(program, nullptr);
		Child &child = children[n_children++];
		child.fd = process.fd.Steal();
	}

	while (Forward()) {}
	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
