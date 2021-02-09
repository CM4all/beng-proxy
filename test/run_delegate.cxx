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

#include "delegate/Glue.hxx"
#include "delegate/Handler.hxx"
#include "delegate/Stock.hxx"
#include "spawn/Config.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "stock/MapStock.hxx"
#include "event/DeferEvent.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char helper_path[] = "./delegate-helper";
static StockMap *delegate_stock;

class MyDelegateHandler final : public DelegateHandler {
	DeferEvent defer_stop;

public:
	MyDelegateHandler(EventLoop &event_loop)
		:defer_stop(event_loop, BIND_THIS_METHOD(Stop)) {}

	void Stop() noexcept {
		delegate_stock_free(delegate_stock);
	}

	void OnDelegateSuccess(UniqueFileDescriptor) override {
		defer_stop.Schedule();
	}

	void OnDelegateError(std::exception_ptr ep) override {
		PrintException(ep);

		defer_stop.Schedule();
	}
};

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: run-delegate PATH\n");
		return 1;
	}

	SpawnConfig spawn_config;

	PInstance instance;

	ChildProcessRegistry child_process_registry(instance.event_loop);
	child_process_registry.SetVolatile();

	LocalSpawnService spawn_service(spawn_config, child_process_registry);

	delegate_stock = delegate_stock_new(instance.event_loop, spawn_service);
	const auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	ChildOptions child_options;

	MyDelegateHandler handler(instance.event_loop);
	CancellablePointer cancel_ptr;
	delegate_stock_open(delegate_stock, AllocatorPtr{pool},
			    helper_path, child_options,
			    argv[1],
			    handler, cancel_ptr);

	instance.event_loop.Dispatch();
}
