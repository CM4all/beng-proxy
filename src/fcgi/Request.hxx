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
class FcgiStock;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
class UniqueFileDescriptor;
class StopwatchPtr;

/**
 * High level FastCGI client.
 *
 * @param args command-line arguments
 */
void
fcgi_request(struct pool *pool, EventLoop &event_loop,
	     FcgiStock *fcgi_stock,
	     const StopwatchPtr &parent_stopwatch,
	     const char *site_name,
	     const CgiAddress &address,
	     HttpMethod method,
	     const char *remote_addr,
	     StringMap &&headers, UnusedIstreamPtr body,
	     UniqueFileDescriptor &&stderr_fd,
	     HttpResponseHandler &handler,
	     CancellablePointer &cancel_ptr) noexcept;
