// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <memory>

enum class HttpMethod : uint_least8_t;
struct HttpAddress;
struct PendingHttpRequest;
class AllocatorPtr;
class EventLoop;
class StopwatchPtr;
class SocketAddress;
class FilteredSocket;
class SocketFilterParams;
class StringMap;
class UnusedIstreamPtr;
class HttpResponseHandler;
class CancellablePointer;

namespace NgHttp2 {

/**
 * Handler for ALPN-related events.  As soon as the TLS handshake
 * finishes, exactly one method is called.
 */
class AlpnHandler {
public:
	/**
	 * A connection error has occurred.  This exact error
	 * conditions will be passed to the #HttpResponseHandler; this
	 * method is just a notification that the handshake has
	 * failed.
	 */
	virtual void OnAlpnError() noexcept = 0;

	/**
	 * The TLS handshake has completed successfully, and HTTP/2
	 * was selected.
	 */
	virtual void OnAlpnNoMismatch() noexcept = 0;

	/**
	 * The TLS handshake has completed successfully, but HTTP/2
	 * was not available.  This method gets a chance to use the
	 * connected socket for HTTP/1.1; the #HttpResponseHandler
	 * will not be invoked.
	 */
	virtual void OnAlpnMismatch(PendingHttpRequest &&pending_request,
				    SocketAddress address,
				    std::unique_ptr<FilteredSocket> &&socket) noexcept = 0;
};

class Stock;

void
SendRequest(AllocatorPtr alloc, EventLoop &event_loop, Stock &stock,
	    const StopwatchPtr &parent_stopwatch,
	    const SocketFilterParams *filter_params,
	    HttpMethod method,
	    const HttpAddress &address,
	    StringMap &&headers, UnusedIstreamPtr body,
	    AlpnHandler *alpn_handler,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr) noexcept;

} // namespace NgHttp2
