// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "event/net/control/Server.hxx"
#include "event/net/control/Handler.hxx"
#include "net/SocketConfig.hxx"
#include "net/Parser.hxx"
#include "event/Loop.hxx"
#include "system/SetupProcess.hxx"
#include "io/Logger.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>

using namespace BengControl;

class DumpControlHandler final : public Handler {
public:
	void OnControlPacket(Server &,
			     Command command,
			     std::span<const std::byte> payload,
			     std::span<UniqueFileDescriptor>,
			     SocketAddress, int uid) override {
		printf("packet command=%u uid=%d length=%zu\n",
		       unsigned(command), uid, payload.size());
	}

	void OnControlError(std::exception_ptr ep) noexcept override {
		PrintException(ep);
	}
};

int main(int argc, char **argv)
try {
	SetLogLevel(5);

	if (argc > 3) {
		fprintf(stderr, "usage: dump-control [LISTEN:PORT [MCAST_GROUP]]\n");
		return 1;
	}

	const char *listen_host = argc >= 2 ? argv[1] : "*";
	const char *mcast_group = argc >= 3 ? argv[2] : NULL;

	SetupProcess();

	EventLoop event_loop;

	SocketConfig config;
	config.bind_address = ParseSocketAddress(listen_host, 1234, true);

	if (mcast_group != nullptr)
		config.multicast_group = ParseSocketAddress(mcast_group, 0, false);

	config.Fixup();

	DumpControlHandler handler;

	Server cs{event_loop, handler, config};

	event_loop.Run();

	return 0;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
