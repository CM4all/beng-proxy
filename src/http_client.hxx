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
 * HTTP client implementation.
 */

#ifndef BENG_PROXY_HTTP_CLIENT_HXX
#define BENG_PROXY_HTTP_CLIENT_HXX

#include "fs/Ptr.hxx"
#include "io/FdType.hxx"
#include "http/Method.h"

#include <stdexcept>

struct pool;
class EventLoop;
class UnusedIstreamPtr;
class SocketDescriptor;
class Lease;
class HttpResponseHandler;
class CancellablePointer;
class HttpHeaders;

/**
 * Error codes for #HttpClientError.
 */
enum class HttpClientErrorCode {
    UNSPECIFIED,

    /**
     * The server has closed the connection before the first response
     * byte.
     */
    REFUSED,

    /**
     * The server has closed the connection prematurely.
     */
    PREMATURE,

    /**
     * A socket I/O error has occurred.
     */
    IO,

    /**
     * Non-HTTP garbage was received.
     */
    GARBAGE,

    /**
     * The server has failed to respond or accept data in time.
     */
    TIMEOUT,
};

class HttpClientError : public std::runtime_error {
    HttpClientErrorCode code;

public:
    HttpClientError(HttpClientErrorCode _code, const char *_msg)
        :std::runtime_error(_msg), code(_code) {}

    HttpClientErrorCode GetCode() const {
        return code;
    }
};

/**
 * Is the specified error a server failure, that justifies
 * blacklisting the server for a while?
 */
bool
IsHttpClientServerFailure(std::exception_ptr ep);

/**
 * Sends a HTTP request on a socket, and passes the response to the
 * handler.
 *
 * @param pool the memory pool; this client holds a reference until
 * the response callback has returned and the response body is closed
 * @param fd a socket to the HTTP server
 * @param fd_type the exact socket type
 * @param lease the lease for the socket
 * @param lease_ctx a context pointer for the lease
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param headers the serialized request headers (optional)
 * @param body the request body (optional)
 * @param expect_100 true to send "Expect: 100-continue" in the
 * presence of a request body
 * @param handler receives the response
 * @param async_ref a handle which may be used to abort the operation
 */
void
http_client_request(struct pool &pool, EventLoop &event_loop,
                    SocketDescriptor fd, FdType fd_type,
                    Lease &lease,
                    const char *peer_name,
                    SocketFilterPtr filter,
                    http_method_t method, const char *uri,
                    HttpHeaders &&headers,
                    UnusedIstreamPtr body, bool expect_100,
                    HttpResponseHandler &handler,
                    CancellablePointer &cancel_ptr);

#endif
