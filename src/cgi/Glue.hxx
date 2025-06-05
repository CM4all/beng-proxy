// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>

enum class HttpMethod : uint_least8_t;
struct pool;
struct CgiAddress;
class StopwatchPtr;
class EventLoop;
class UnusedIstreamPtr;
class SpawnService;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;

/**
 * Run a CGI script.
 */
void
cgi_new(SpawnService &spawn_service, EventLoop &event_loop,
	struct pool *pool,
	const StopwatchPtr &parent_stopwatch,
	HttpMethod method,
	const CgiAddress *address,
	const char *remote_addr,
	const StringMap &headers, UnusedIstreamPtr body,
	HttpResponseHandler &_handler,
	CancellablePointer &cancel_ptr);
