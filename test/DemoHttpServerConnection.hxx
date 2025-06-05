// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/server/Handler.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "memory/SlicePool.hxx"
#include "event/DeferEvent.hxx"
#include "event/FineTimerEvent.hxx"
#include "io/FdType.hxx"
#include "util/Cancellable.hxx"

template<typename T> class UniquePoolPtr;
struct HttpServerConnection;
class FilteredSocket;
class SocketAddress;

class DemoHttpServerConnection
	: protected HttpServerConnectionHandler, HttpServerRequestHandler,
	  Cancellable
{
public:
	enum class Mode {
		MODE_NULL,
		MIRROR,

		/**
		 * Defer the response, meanwhile "hold" the request
		 * body.
		 */
		DEFER_MIRROR,

		/**
		 * Response body of unknown length with keep-alive disabled.
		 * Response body ends when socket is closed.
		 */
		CLOSE,

		DUMMY,
		FIXED,
		HUGE_,
		HOLD,
		BLOCK,
		NOP,

		/**
		 * Close the kept-alive connection when the second
		 * request is received.
		 */
		FAILING_KEEPALIVE,
	};

private:
	SlicePool request_slice_pool{8192, 256, "Requests"};

	HttpServerConnection *connection;

	IncomingHttpRequest *current_request;

	UnusedHoldIstreamPtr request_body;

	DeferEvent response_timer;

	const Mode mode;

	bool first = true;

public:
	DemoHttpServerConnection(struct pool &pool, EventLoop &event_loop,
				 UniquePoolPtr<FilteredSocket> socket,
				 SocketAddress address,
				 Mode _mode) noexcept;

	~DemoHttpServerConnection() noexcept;

	auto &GetEventLoop() const noexcept {
		return response_timer.GetEventLoop();
	}

private:
	void OnDeferred() noexcept;
	void OnResponseTimer() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept final {
		request_body.Clear();
	}

protected:
	/* virtual methods from class HttpServerConnectionHandler */
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;
	void HttpConnectionError(std::exception_ptr e) noexcept override;
	void HttpConnectionClosed() noexcept override;
};
