/*
 * Copyright 2007-2022 CM4all GmbH
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

#pragma once

#include "cluster/StickyHash.hxx"
#include "http/Method.h"

struct pool;
class EventLoop;
class StopwatchPtr;
class UnusedIstreamPtr;
class FilteredSocketBalancer;
class SocketFilterFactory;
struct HttpAddress;
class HttpResponseHandler;
class CancellablePointer;
class StringMap;

/**
 * High level HTTP client.
 *
 * @param sticky_hash a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 */
void
http_request(struct pool &pool, EventLoop &event_loop,
	     FilteredSocketBalancer &fs_balancer,
	     const StopwatchPtr &parent_stopwatch,
	     sticky_hash_t sticky_hash,
	     SocketFilterFactory *filter_factory,
	     http_method_t method,
	     const HttpAddress &address,
	     StringMap &&headers, UnusedIstreamPtr body,
	     HttpResponseHandler &handler,
	     CancellablePointer &cancel_ptr);
