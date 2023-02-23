// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "cluster/StickyHash.hxx"

#include <cstdint>

enum class HttpMethod : uint_least8_t;
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
	     HttpMethod method,
	     const HttpAddress &address,
	     StringMap &&headers, UnusedIstreamPtr body,
	     HttpResponseHandler &handler,
	     CancellablePointer &cancel_ptr);
