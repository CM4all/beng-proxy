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

#ifndef BENG_HTTP_SERVER_HANDLER_HXX
#define BENG_HTTP_SERVER_HANDLER_HXX

#include "http/Status.h"

#include <exception>

#include <stdint.h>

struct HttpServerRequest;
class CancellablePointer;

class HttpServerConnectionHandler {
public:
    /**
     * Called after the empty line after the last header has been
     * parsed.  Several attributes can be evaluated (method, uri,
     * headers; but not the body).  This can be used to collect
     * metadata for LogHttpRequest().
     */
    virtual void RequestHeadersFinished(const HttpServerRequest &) noexcept {};

    virtual void HandleHttpRequest(HttpServerRequest &request,
                                   CancellablePointer &cancel_ptr) noexcept = 0;

    /**
     * @param length the number of response body (payload) bytes sent
     * to our HTTP client, or negative if there was no response body
     * (which is different from "empty response body")
     * @param bytes_received the number of raw bytes received from our
     * HTTP client
     * @param bytes_sent the number of raw bytes sent to our HTTP
     * client (which includes status line, headers and transport
     * encoding overhead such as chunk headers)
     */
    virtual void LogHttpRequest(HttpServerRequest &request,
                                http_status_t status, int64_t length,
                                uint64_t bytes_received, uint64_t bytes_sent) noexcept = 0;

    /**
     * A fatal protocol level error has occurred, and the connection
     * was closed.
     *
     * This will be called instead of HttpConnectionClosed().
     */
    virtual void HttpConnectionError(std::exception_ptr e) noexcept = 0;

    virtual void HttpConnectionClosed() noexcept = 0;
};

#endif
