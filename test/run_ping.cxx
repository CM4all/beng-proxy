// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "event/Loop.hxx"
#include "net/Ping.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/Parser.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>
#include <stdlib.h>

static bool success;

class MyPingClientHandler final : public PingClientHandler {
public:
	void PingResponse() noexcept override {
		success = true;
		printf("ok\n");
	}

	void PingTimeout() noexcept override {
		fprintf(stderr, "timeout\n");
	}

	void PingError(std::exception_ptr ep) noexcept override {
		PrintException(ep);
	}
};

int
main(int argc, char **argv) noexcept
try {
	if (argc != 2) {
		fprintf(stderr, "usage: run-ping IP\n");
		return EXIT_FAILURE;
	}

	const auto address = ParseSocketAddress(argv[1], 0, false);

	EventLoop event_loop;

	MyPingClientHandler handler;
	PingClient client(event_loop, handler);
	client.Start(address);

	event_loop.Run();

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
