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
 * AJPv13 client implementation.
 */

#ifndef BENG_PROXY_AJP_CLIENT_HXX
#define BENG_PROXY_AJP_CLIENT_HXX

#include "io/FdType.hxx"
#include "http/Method.h"

struct pool;
class EventLoop;
class Istream;
class SocketDescriptor;
class Lease;
class HttpResponseHandler;
class StringMap;
class CancellablePointer;

/**
 * Sends a HTTP request on a socket to an AJPv13 server, and passes
 * the response to the handler.
 *
 * @param pool the memory pool
 * @param fd a socket to the HTTP server
 * @param fd_type the exact socket type
 * @param lease the lease for the socket
 * @param protocol the name of the original protocol, e.g. "http"
 * @param remote_addr the address of the original client
 * @param remote_host the host name of the original client
 * @param server_name the host name of the server
 * @param server_port the port to which the client connected
 * @param is_ssl true if the client is using SSL
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param headers the serialized request headers (optional)
 * @param body the request body (optional)
 * @param handler receives the response
 * @param async_ref a handle which may be used to abort the operation
 */
void
ajp_client_request(struct pool &pool, EventLoop &event_loop,
                   SocketDescriptor fd, FdType fd_type,
                   Lease &lease,
                   const char *protocol, const char *remote_addr,
                   const char *remote_host, const char *server_name,
                   unsigned server_port, bool is_ssl,
                   http_method_t method, const char *uri,
                   StringMap &headers,
                   Istream *body,
                   HttpResponseHandler &handler,
                   CancellablePointer &cancel_ptr);

#endif
