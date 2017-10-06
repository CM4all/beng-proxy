/*
 * Copyright 2007-2017 Content Management AG
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

#include "Client.hxx"
#include "Config.hxx"
#include "Filter.hxx"
#include "ssl/Basic.hxx"
#include "ssl/Ctx.hxx"
#include "ssl/Error.hxx"
#include "io/Logger.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "util/ScopeExit.hxx"

static SslCtx ssl_client_ctx;

void
ssl_client_init()
{
    try {
        ssl_client_ctx = CreateBasicSslCtx(false);
    } catch (const SslError &e) {
        LogConcat(1, "ssl_client", "ssl_factory_new() failed: ", e.what());
    }
}

void
ssl_client_deinit()
{
    ssl_client_ctx.reset();
}

const SocketFilter &
ssl_client_get_filter()
{
    return thread_socket_filter;;
}

void *
ssl_client_create(EventLoop &event_loop,
                  const char *hostname)
{
    UniqueSSL ssl(SSL_new(ssl_client_ctx.get()));
    if (!ssl)
        throw SslError("SSL_new() failed");

    SSL_set_connect_state(ssl.get());

    (void)hostname; // TODO: use this parameter

    auto f = ssl_filter_new(std::move(ssl));

    auto &queue = thread_pool_get_queue(event_loop);
    return new ThreadSocketFilter(event_loop, queue,
                                  &ssl_filter_get_handler(*f));
}
