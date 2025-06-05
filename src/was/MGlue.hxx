// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <span>

enum class HttpMethod : uint_least8_t;
struct pool;
struct CgiAddress;
class StopwatchPtr;
class SocketAddress;
class UnusedIstreamPtr;
class MultiWasStock;
class RemoteWasStock;
class StringMap;
class WasMetricsHandler;
class HttpResponseHandler;
class CancellablePointer;
struct ChildOptions;

/**
 * High level Multi-WAS client.
 *
 * @param args command-line arguments
 */
void
SendMultiWasRequest(struct pool &pool, MultiWasStock &was_stock,
		    const StopwatchPtr &parent_stopwatch,
		    const char *site_name,
		    const CgiAddress &address,
		    const char *remote_host,
		    HttpMethod method,
		    StringMap &&headers, UnusedIstreamPtr body,
		    WasMetricsHandler *metrics_handler,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept;

/**
 * High level Remote-WAS client.
 *
 * @param args command-line arguments
 */
void
SendRemoteWasRequest(struct pool &pool, RemoteWasStock &was_stock,
		     const StopwatchPtr &parent_stopwatch,
		     const CgiAddress &address,
		     const char *remote_host,
		     HttpMethod method,
		     StringMap &&headers, UnusedIstreamPtr body,
		     WasMetricsHandler *metrics_handler,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept;
