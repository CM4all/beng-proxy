// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>

enum class HttpMethod : uint_least8_t;
struct pool;
struct CgiAddress;
class EventLoop;
class UnusedIstreamPtr;
class TcpBalancer;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
class UniqueFileDescriptor;
class StopwatchPtr;

/**
 * High level FastCGI client for remote FastCGI servers.
 */
void
fcgi_remote_request(struct pool *pool, EventLoop &event_loop,
		    TcpBalancer *tcp_balancer,
		    const StopwatchPtr &parent_stopwatch,
		    const CgiAddress &address,
		    HttpMethod method,
		    const char *remote_addr,
		    StringMap &&headers, UnusedIstreamPtr body,
		    UniqueFileDescriptor stderr_fd,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr);
