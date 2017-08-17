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

#include "cgi_glue.hxx"
#include "cgi_address.hxx"
#include "cgi_client.hxx"
#include "cgi_launch.hxx"
#include "abort_flag.hxx"
#include "stopwatch.hxx"
#include "http_response.hxx"
#include "istream/istream.hxx"

void
cgi_new(SpawnService &spawn_service, EventLoop &event_loop,
        struct pool *pool, http_method_t method,
        const CgiAddress *address,
        const char *remote_addr,
        const StringMap &headers, Istream *body,
        HttpResponseHandler &handler,
        CancellablePointer &cancel_ptr)
{
    auto *stopwatch = stopwatch_new(pool, address->path);

    AbortFlag abort_flag(cancel_ptr);

    Istream *input;

    try {
        input = cgi_launch(event_loop, pool, method, address,
                                remote_addr, headers, body,
                                spawn_service);
    } catch (...) {
        if (abort_flag.aborted) {
            /* the operation was aborted - don't call the
               http_response_handler */
            return;
        }

        handler.InvokeError(std::current_exception());
        return;
    }

    stopwatch_event(stopwatch, "fork");

    cgi_client_new(*pool, stopwatch, *input, handler, cancel_ptr);
}
