// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "system/SetupProcess.hxx"
#include "event/net/UdpListener.hxx"
#include "event/net/UdpHandler.hxx"
#include "net/SocketConfig.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/Parser.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/Loop.hxx"
#include "io/Logger.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>

class DumpUdpHandler final : public UdpHandler {
public:
	/* virtual methods from class UdpHandler */
	bool OnUdpDatagram(std::span<const std::byte> payload,
			   [[maybe_unused]] std::span<UniqueFileDescriptor> fds,
			   gcc_unused SocketAddress address, int uid) override {
		printf("packet: %zu uid=%d\n", payload.size(), uid);
		return true;
	}

	void OnUdpError(std::exception_ptr ep) noexcept override {
		PrintException(ep);
	}
};

int main(int argc, char **argv)
try {
	SetLogLevel(5);

	if (argc > 3) {
		fprintf(stderr, "usage: dump-udp [LISTEN:PORT [MCAST_GROUP]]\n");
		return 1;
	}

	const char *listen_host = argc >= 2 ? argv[1] : "*";
	const char *mcast_group = argc >= 3 ? argv[2] : nullptr;

	SetupProcess();

	EventLoop event_loop;

	DumpUdpHandler handler;

	SocketConfig config;
	config.bind_address = ParseSocketAddress(listen_host, 1234, true);

	if (mcast_group != nullptr)
		config.multicast_group = ParseSocketAddress(mcast_group, 0, false);

	UdpListener udp(event_loop, config.Create(SOCK_DGRAM), handler);

	event_loop.Run();

	return 0;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
