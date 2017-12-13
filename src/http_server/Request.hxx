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

/*
 * HTTP server implementation.
 */

#ifndef BENG_HTTP_SERVER_REQUEST_HXX
#define BENG_HTTP_SERVER_REQUEST_HXX

#include "strmap.hxx"
#include "net/SocketAddress.hxx"
#include "http/Method.h"
#include "istream/UnusedPtr.hxx"

struct pool;
struct StringView;
class StringMap;
class Istream;
struct HttpServerConnection;

struct HttpServerRequest {
    struct pool &pool;
    HttpServerConnection &connection;

    const SocketAddress local_address, remote_address;

    /**
     * The local address (host and port) that was connected to.
     */
    const char *const local_host_and_port;

    /**
     * The address of the client, without the port number.
     */
    const char *const remote_host;

    /* request metadata */
    const http_method_t method;
    char *const uri;
    StringMap headers;

    /**
     * The request body.  The handler is responsible for closing this
     * istream.
     */
    UnusedIstreamPtr body;

    HttpServerRequest(struct pool &_pool, HttpServerConnection &_connection,
                      SocketAddress _local_address,
                      SocketAddress _remote_address,
                      const char *_local_host_and_port,
                      const char *_remote_host,
                      http_method_t _method,
                      StringView _uri);

    HttpServerRequest(const HttpServerRequest &) = delete;
    HttpServerRequest &operator=(const HttpServerRequest &) = delete;

    bool HasBody() const {
        return body;
    }

    /**
     * Close the still-unused request body.  This is a no-op if there
     * is no request body.
     */
    void CheckCloseUnusedBody() {
        body.Clear();
    }
};

#endif
