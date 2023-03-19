// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "BlockingResourceLoader.hxx"
#include "MirrorResourceLoader.hxx"
#include "http/cache/FilterCache.hxx"
#include "strmap.hxx"
#include "http/ResponseHandler.hxx"
#include "ResourceAddress.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_string.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "pool/RootPool.hxx"
#include "event/Loop.hxx"
#include "util/PrintException.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

#include <stdlib.h>

static void
TestCancelBlocking()
{
	struct Context final : HttpResponseHandler {
		EventLoop event_loop;
		RootPool root_pool;

		BlockingResourceLoader resource_loader;
		FilterCache *fcache = filter_cache_new(root_pool, 65536,
						       event_loop, resource_loader);

		~Context() noexcept {
			filter_cache_close(fcache);
		}

		/* virtual methods from class HttpResponseHandler */
		void OnHttpResponse(HttpStatus, StringMap &&,
				    UnusedIstreamPtr) noexcept override {
			abort();
		}

		void OnHttpError(std::exception_ptr) noexcept override {
			abort();
		}
	};

	Context context;
	CancellablePointer cancel_ptr;

	auto request_pool = pool_new_linear(context.root_pool, "Request", 8192);
	filter_cache_request(*context.fcache, request_pool, nullptr,
			     nullptr, nullptr,
			     "foo", HttpStatus::OK, {},
			     istream_string_new(*request_pool, "bar"),
			     context, cancel_ptr);

	cancel_ptr.Cancel();
}

static void
TestNoBody()
{
	struct Context final : HttpResponseHandler {
		EventLoop event_loop;
		RootPool root_pool;

		MirrorResourceLoader resource_loader;
		FilterCache *fcache = filter_cache_new(root_pool, 65536,
						       event_loop, resource_loader);

		~Context() noexcept {
			filter_cache_close(fcache);
		}

		/* virtual methods from class HttpResponseHandler */
		void OnHttpResponse(HttpStatus, StringMap &&,
				    UnusedIstreamPtr) noexcept override {
		}

		void OnHttpError(std::exception_ptr) noexcept override {
			abort();
		}
	};

	Context context;
	CancellablePointer cancel_ptr;

	auto request_pool = pool_new_linear(context.root_pool, "Request", 8192);
	filter_cache_request(*context.fcache, *request_pool, nullptr,
			     nullptr, nullptr,
			     "foo", HttpStatus::OK, {},
			     nullptr,
			     context, cancel_ptr);
}

int
main(int, char **)
try {
		TestCancelBlocking();
		TestNoBody();
		return EXIT_SUCCESS;
} catch (const std::exception &e) {
		PrintException(e);
		return EXIT_FAILURE;
}
