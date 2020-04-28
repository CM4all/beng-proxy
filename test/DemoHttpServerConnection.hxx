/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "http_server/Handler.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "event/TimerEvent.hxx"
#include "io/FdType.hxx"
#include "util/Cancellable.hxx"

template<typename T> class UniquePoolPtr;
struct HttpServerConnection;
class FilteredSocket;
class SocketAddress;

class DemoHttpServerConnection : protected HttpServerConnectionHandler, Cancellable
{
public:
	enum class Mode {
		MODE_NULL,
		MIRROR,

		/**
		 * Response body of unknown length with keep-alive disabled.
		 * Response body ends when socket is closed.
		 */
		CLOSE,

		DUMMY,
		FIXED,
		HUGE_,
		HOLD,
		NOP,

		/**
		 * Close the kept-alive connection when the second
		 * request is received.
		 */
		FAILING_KEEPALIVE,
	};

private:
	HttpServerConnection *connection;

	UnusedHoldIstreamPtr request_body;

	TimerEvent timer;

	const Mode mode;

	bool first = true;

public:
	DemoHttpServerConnection(struct pool &pool, EventLoop &event_loop,
				 UniquePoolPtr<FilteredSocket> socket,
				 SocketAddress address,
				 Mode _mode) noexcept;

	~DemoHttpServerConnection() noexcept;

	auto &GetEventLoop() const noexcept {
		return timer.GetEventLoop();
	}

private:
	void OnTimer() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept final {
		request_body.Clear();
		timer.Cancel();
	}

protected:
	/* virtual methods from class HttpServerConnectionHandler */
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;
	void HttpConnectionError(std::exception_ptr e) noexcept override;
	void HttpConnectionClosed() noexcept override;
};
