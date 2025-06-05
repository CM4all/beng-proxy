// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "cluster/ConnectBalancer.hxx"
#include "cluster/BalancerMap.hxx"
#include "cluster/AddressList.hxx"
#include "cluster/AddressListBuilder.hxx"
#include "event/net/ConnectSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/FailureManager.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "pool/PSocketAddress.hxx"
#include "AllocatorPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Context final : TestInstance, ConnectSocketHandler {
	FailureManager failure_manager;
	BalancerMap balancer;

	enum {
		NONE, SUCCESS, TIMEOUT, ERROR,
	} result = TIMEOUT;

	UniqueSocketDescriptor fd;
	std::exception_ptr error;

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor new_fd) noexcept override {
		result = SUCCESS;
		fd = std::move(new_fd);
	}

	void OnSocketConnectTimeout() noexcept override {
		result = TIMEOUT;
	}

	void OnSocketConnectError(std::exception_ptr ep) noexcept override {
		result = ERROR;
		error = std::move(ep);
	}
};

/*
 * main
 *
 */

int
main(int argc, char **argv)
try {
	if (argc <= 1) {
		fprintf(stderr, "Usage: run-client-balancer ADDRESS ...\n");
		return EXIT_FAILURE;
	}

	/* initialize */

	Context ctx;

	const auto pool = pool_new_linear(ctx.root_pool, "test", 8192);
	AllocatorPtr alloc(pool);

	AddressListBuilder address_list_builder;

	static constexpr auto hints = MakeAddrInfo(AI_ADDRCONFIG, AF_UNSPEC,
						   SOCK_STREAM);

	for (int i = 1; i < argc; ++i)
		address_list_builder.Add(alloc, Resolve(argv[i], 80, &hints));

	const auto address_list = address_list_builder.Finish(alloc);

	/* connect */

	CancellablePointer cancel_ptr;
	client_balancer_connect(ctx.event_loop, *pool, ctx.balancer,
				ctx.failure_manager,
				false, SocketAddress::Null(),
				0, address_list, std::chrono::seconds(30),
				ctx, cancel_ptr);

	ctx.event_loop.Run();

	assert(ctx.result != Context::NONE);

	/* cleanup */

	switch (ctx.result) {
	case Context::NONE:
		break;

	case Context::SUCCESS:
		return EXIT_SUCCESS;

	case Context::TIMEOUT:
		fprintf(stderr, "timeout\n");
		return EXIT_FAILURE;

	case Context::ERROR:
		PrintException(ctx.error);
		return EXIT_FAILURE;
	}

	assert(false);
	return EXIT_FAILURE;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
