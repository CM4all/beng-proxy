/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "BlockingResourceLoader.hxx"
#include "MirrorResourceLoader.hxx"
#include "fcache.hxx"
#include "strmap.hxx"
#include "HttpResponseHandler.hxx"
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
        void OnHttpResponse(http_status_t, StringMap &&,
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
                         "foo", HTTP_STATUS_OK, {},
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
        void OnHttpResponse(http_status_t, StringMap &&,
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
			 "foo", HTTP_STATUS_OK, {},
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
