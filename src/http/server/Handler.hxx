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
