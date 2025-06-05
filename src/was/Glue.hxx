// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>

enum class HttpMethod : uint_least8_t;
struct pool;
class StopwatchPtr;
struct CgiAddress;
struct StringWithHash;
class UnusedIstreamPtr;
class WasStock;
class WasMetricsHandler;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;

/**
 * High level WAS client.
 */
void
was_request(struct pool &pool, WasStock &was_stock,
	    const StopwatchPtr &parent_stopwatch,
	    const char *site_name,
	    const CgiAddress &address,
	    const char *remote_host,
	    HttpMethod method,
	    StringMap &&headers, UnusedIstreamPtr body,
	    WasMetricsHandler *metrics_handler,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr);
