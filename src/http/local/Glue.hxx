// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>

#include "http/Method.hxx"
struct pool;
class EventLoop;
class StopwatchPtr;
class UnusedIstreamPtr;
class LhttpStock;
struct LhttpAddress;
class HttpResponseHandler;
class CancellablePointer;
class StringMap;

/**
 * High level "Local HTTP" client.
 */
void
lhttp_request(struct pool &pool, EventLoop &event_loop,
	      LhttpStock &lhttp_stock,
	      const StopwatchPtr &parent_stopwatch,
	      const char *site_name,
	      const LhttpAddress &address,
	      HttpMethod method,
	      StringMap &&headers, UnusedIstreamPtr body,
	      HttpResponseHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept;
