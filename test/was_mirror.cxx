// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "was/Server.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "http/Status.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/Logger.hxx"

#include <stdio.h>
#include <stdlib.h>

struct Instance final : TestInstance, WasServerHandler {
	WasServer *server;

	void OnWasRequest([[maybe_unused]] struct pool &pool,
			  [[maybe_unused]] HttpMethod method,
			  [[maybe_unused]] const char *uri, StringMap &&headers,
			  UnusedIstreamPtr body) noexcept override {
		const bool has_body = body;
		server->SendResponse(has_body ? HttpStatus::OK : HttpStatus::NO_CONTENT,
				     std::move(headers), std::move(body));
	}

	void OnWasClosed() noexcept override {}
};

int
main(int, char **)
{
	SetLogLevel(5);

	WasSocket socket{
		UniqueSocketDescriptor{AdoptTag{}, 3},
		UniqueFileDescriptor{AdoptTag{}, STDIN_FILENO},
		UniqueFileDescriptor{AdoptTag{}, STDOUT_FILENO},
	};

	Instance instance;

	instance.server = NewFromPool<WasServer>(instance.root_pool,
						 instance.root_pool,
						 instance.event_loop,
						 std::move(socket),
						 instance);

	instance.event_loop.Run();

	instance.server->Free();
}
