// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "net/RBindSocket.hxx"
#include "net/SocketError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/FileDescriptor.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
try {
	if (argc < 3) {
		fprintf(stderr, "usage: LaunchLocalHTTP BIND_ADDRESS PROGRAM [ARGS...]\n");
		return EXIT_FAILURE;
	}

	auto s = ResolveBindStreamSocket(argv[1], 8080);

	if (!s.Listen(64))
		throw MakeSocketError("Failed to listen");

	auto fd = s.ToFileDescriptor();
	fd.CheckDuplicate(FileDescriptor{STDIN_FILENO});

	execv(argv[2], argv + 2);

	perror("exec failed");
	return EXIT_FAILURE;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
