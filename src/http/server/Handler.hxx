// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

struct IncomingHttpRequest;
class CancellablePointer;
class StopwatchPtr;

class HttpServerConnectionHandler {
public:
	/**
	 * Called after the empty line after the last header has been
	 * parsed.  Several attributes can be evaluated (method, uri,
	 * headers; but not the body).  This can be used to collect
	 * metadata for LogHttpRequest().
	 */
	virtual void RequestHeadersFinished(IncomingHttpRequest &) noexcept {}

	/**
	 * Called after sending a response was finished successfully.
	 * This can be used to track the timing of requests and
	 * responses.
	 *
	 * Note: this is not implemented for HTTP/2 (class
	 * NgHttp2::ServerConnection).
	 */
	virtual void ResponseFinished() noexcept {}

	/**
	 * A fatal protocol level error has occurred, and the connection
	 * was closed.
	 *
	 * This will be called instead of HttpConnectionClosed().
	 */
	virtual void HttpConnectionError(std::exception_ptr e) noexcept = 0;

	virtual void HttpConnectionClosed() noexcept = 0;
};

class HttpServerRequestHandler {
public:
	virtual void HandleHttpRequest(IncomingHttpRequest &request,
				       const StopwatchPtr &parent_stopwatch,
				       CancellablePointer &cancel_ptr) noexcept = 0;
};
