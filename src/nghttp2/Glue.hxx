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
class SocketFilterFactory;
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
	    SocketFilterFactory *filter_factory,
	    HttpMethod method,
	    const HttpAddress &address,
	    StringMap &&headers, UnusedIstreamPtr body,
	    AlpnHandler *alpn_handler,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr) noexcept;

} // namespace NgHttp2
