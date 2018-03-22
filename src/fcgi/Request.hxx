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

#ifndef BENG_PROXY_FCGI_REQUEST_HXX
#define BENG_PROXY_FCGI_REQUEST_HXX

#include "http/Method.h"

struct pool;
class EventLoop;
class UnusedIstreamPtr;
struct FcgiStock;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
struct ChildOptions;
template<typename T> struct ConstBuffer;

/**
 * High level FastCGI client.
 *
 * @param jail run the FastCGI application with JailCGI?
 * @param args command-line arguments
 */
void
fcgi_request(struct pool *pool, EventLoop &event_loop,
             FcgiStock *fcgi_stock,
             const char *site_name,
             const ChildOptions &options,
             const char *action,
             const char *path,
             ConstBuffer<const char *> args,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             const char *remote_addr,
             const StringMap &headers, UnusedIstreamPtr body,
             ConstBuffer<const char *> params,
             int stderr_fd,
             HttpResponseHandler &handler,
             CancellablePointer &cancel_ptr);

#endif
