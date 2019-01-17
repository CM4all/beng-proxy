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

#ifndef BENG_PROXY_WAS_SERVER_HXX
#define BENG_PROXY_WAS_SERVER_HXX

#include "http/Method.h"
#include "http/Status.h"

struct pool;
class FileDescriptor;
class SocketDescriptor;
class EventLoop;
class UnusedIstreamPtr;
struct lease;
class StringMap;
class HttpResponseHandler;
class WasServer;

class WasServerHandler {
public:
    virtual void OnWasRequest(struct pool &pool, http_method_t method,
                              const char *uri, StringMap &&headers,
                              UnusedIstreamPtr body) noexcept = 0;

    virtual void OnWasClosed() noexcept = 0;
};

/**
 * Creates a WAS server, waiting for HTTP requests on the specified
 * socket.
 *
 * @param pool the memory pool
 * @param control_fd a control socket to the WAS client
 * @param input_fd a data pipe for the request body
 * @param output_fd a data pipe for the response body
 * @param handler a callback function which receives events
 * @param ctx a context pointer for the callback function
 */
WasServer *
was_server_new(struct pool &pool, EventLoop &event_loop,
               SocketDescriptor control_fd,
               FileDescriptor input_fd, int output_fd,
               WasServerHandler &handler);

void
was_server_free(WasServer *server);

void
was_server_response(WasServer &server, http_status_t status,
                    StringMap &&headers, UnusedIstreamPtr body) noexcept;

#endif
