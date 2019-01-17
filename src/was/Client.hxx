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

#ifndef BENG_PROXY_WAS_CLIENT_HXX
#define BENG_PROXY_WAS_CLIENT_HXX

#include "http/Method.h"

struct pool;
struct Stopwatch;
class FileDescriptor;
class SocketDescriptor;
class EventLoop;
class UnusedIstreamPtr;
class WasLease;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
template<typename T> struct ConstBuffer;

/**
 * Web Application Socket client.
 *
 * Sends a HTTP request on a socket to a WAS server, and passes the
 * response to the handler.
 *
 * @param pool the memory pool; this client holds a reference until
 * the response callback has returned and the response body is closed
 * @param control_fd a control socket to the WAS server
 * @param input_fd a data pipe for the response body
 * @param output_fd a data pipe for the request body
 * @param lease the lease for both sockets
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param script_name the URI part of the script
 * @param path_info the URI part following the script name
 * @param query_string the query string (without the question mark)
 * @param headers the request headers (optional)
 * @param body the request body (optional)
 * @param params application specific parameters
 * @param handler receives the response
 * @param cancel_ptr a handle which may be used to abort the operation
 */
void
was_client_request(struct pool &pool, EventLoop &event_loop,
                   Stopwatch *stopwatch,
                   SocketDescriptor control_fd,
                   FileDescriptor input_fd, FileDescriptor output_fd,
                   WasLease &lease,
                   http_method_t method, const char *uri,
                   const char *script_name, const char *path_info,
                   const char *query_string,
                   const StringMap &headers, UnusedIstreamPtr body,
                   ConstBuffer<const char *> params,
                   HttpResponseHandler &handler,
                   CancellablePointer &cancel_ptr);

#endif
