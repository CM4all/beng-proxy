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

#include "cluster/ConnectBalancer.hxx"
#include "cluster/BalancerMap.hxx"
#include "cluster/AddressList.hxx"
#include "event/net/ConnectSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/FailureManager.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "AllocatorPtr.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Context final : PInstance, ConnectSocketHandler {
	FailureManager failure_manager;
	BalancerMap balancer;

	enum {
		NONE, SUCCESS, TIMEOUT, ERROR,
	} result = TIMEOUT;

	UniqueSocketDescriptor fd;
	std::exception_ptr error;

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor &&new_fd) noexcept override {
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

	AddressList address_list;

	static constexpr auto hints = MakeAddrInfo(AI_ADDRCONFIG, AF_UNSPEC,
						   SOCK_STREAM);

	for (int i = 1; i < argc; ++i)
		address_list.Add(alloc, Resolve(argv[i], 80, &hints));

	/* connect */

	CancellablePointer cancel_ptr;
	client_balancer_connect(ctx.event_loop, *pool, ctx.balancer,
				ctx.failure_manager,
				false, SocketAddress::Null(),
				0, address_list, std::chrono::seconds(30),
				ctx, cancel_ptr);

	ctx.event_loop.Dispatch();

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
