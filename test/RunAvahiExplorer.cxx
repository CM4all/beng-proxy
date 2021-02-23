/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "avahi/Check.hxx"
#include "avahi/Client.hxx"
#include "avahi/Explorer.hxx"
#include "avahi/ExplorerListener.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "net/ToString.hxx"
#include "util/PrintException.hxx"

class Instance final : Avahi::ServiceExplorerListener {
	EventLoop event_loop;
	ShutdownListener shutdown_listener;
	Avahi::Client client;
	Avahi::ServiceExplorer explorer;

public:
	explicit Instance(const char *service)
		:shutdown_listener(event_loop, BIND_THIS_METHOD(OnShutdown)),
		 client(event_loop, "RunAvahiExplorer"),
		 explorer(client, *this,
			  AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
			  service, nullptr) {
		shutdown_listener.Enable();
	}

	void Dispatch() {
		event_loop.Dispatch();
	}

private:
	void OnShutdown() noexcept {
		event_loop.Break();
	}

	/* virtual methods from class AvahiServiceExplorerListener */
	void OnAvahiNewObject(const std::string &key,
			      SocketAddress address) noexcept override {
		char buffer[1024];

		printf("new '%s' at %s\n", key.c_str(),
		       ToString(buffer, sizeof(buffer), address, "?"));
	}

	void OnAvahiRemoveObject(const std::string &key) noexcept override {
		printf("remove '%s'\n", key.c_str());
	}
};

int
main(int argc, char **argv)
try {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s SERVICE\n", argv[0]);
		return EXIT_FAILURE;
	}

	const auto service_type = MakeZeroconfServiceType(argv[1], "_tcp");

	Instance instance(service_type.c_str());
	instance.Dispatch();

	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
