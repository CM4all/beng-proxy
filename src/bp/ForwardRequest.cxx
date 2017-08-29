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

#include "ForwardRequest.hxx"
#include "Request.hxx"
#include "http_server/Request.hxx"
#include "header_forward.hxx"

ForwardRequest
request_forward(Request &request2,
                const struct header_forward_settings &header_forward,
                const char *host_and_port, const char *uri,
                bool exclude_host)
{
    const auto &request = request2.request;

    assert(!request.HasBody() || request2.body != nullptr);

    http_method_t method;
    Istream *body;

    /* send a request body? */

    if (request2.processor_focus) {
        /* reserve method+body for the processor, and
           convert this request to a GET */

        method = HTTP_METHOD_GET;
        body = nullptr;
    } else {
        /* forward body (if any) to the real server */

        method = request.method;
        body = request2.body;
        request2.body = nullptr;
    }

    /* generate request headers */

    return ForwardRequest(method,
                          forward_request_headers(request2.pool, request.headers,
                                                  request.local_host_and_port,
                                                  request.remote_host,
                                                  exclude_host,
                                                  body != nullptr,
                                                  !request2.IsProcessorEnabled(),
                                                  !request2.IsTransformationEnabled(),
                                                  !request2.IsTransformationEnabled(),
                                                  header_forward,
                                                  request2.session_cookie,
                                                  request2.GetRealmSession().get(),
                                                  host_and_port, uri),
                          body);
}
