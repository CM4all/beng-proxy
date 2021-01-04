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

#include "was/Server.hxx"
#include "istream/UnusedPtr.hxx"
#include "PInstance.hxx"
#include "fb_pool.hxx"
#include "pool/pool.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/Logger.hxx"
#include "io/SpliceSupport.hxx"

#include <stdio.h>
#include <stdlib.h>

struct Instance final : PInstance, WasServerHandler {
	WasServer *server;

	void OnWasRequest(gcc_unused struct pool &pool,
			  gcc_unused http_method_t method,
			  gcc_unused const char *uri, StringMap &&headers,
			  UnusedIstreamPtr body) noexcept override {
		const bool has_body = body;
		server->SendResponse(has_body ? HTTP_STATUS_OK : HTTP_STATUS_NO_CONTENT,
				     std::move(headers), std::move(body));
	}

	void OnWasClosed() noexcept override {}
};

int
main(int, char **)
{
	SetLogLevel(5);

	WasSocket socket{
		UniqueSocketDescriptor(3),
		UniqueFileDescriptor(STDIN_FILENO),
		UniqueFileDescriptor(STDOUT_FILENO),
	};

	direct_global_init();
	const ScopeFbPoolInit fb_pool_init;

	Instance instance;

	instance.server = NewFromPool<WasServer>(instance.root_pool,
						 instance.root_pool,
						 instance.event_loop,
						 std::move(socket),
						 instance);

	instance.event_loop.Dispatch();

	instance.server->Free();
}
