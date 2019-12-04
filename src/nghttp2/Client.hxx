/*
 * Copyright 2007-2019 Content Management AG
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

#include "Session.hxx"
#include "fs/Ptr.hxx"
#include "event/net/BufferedSocket.hxx"
#include "event/DeferEvent.hxx"
#include "http/Method.h"

#include <boost/intrusive/list.hpp>

#include <memory>

struct pool;
class StringMap;
class StopwatchPtr;
class UniqueSocketDescriptor;
class HttpResponseHandler;
class UnusedIstreamPtr;
class CancellablePointer;
class FilteredSocket;

namespace NgHttp2 {

class ConnectionHandler {
public:
	virtual void OnNgHttp2ConnectionIdle() noexcept = 0;
	virtual void OnNgHttp2ConnectionError(std::exception_ptr e) noexcept = 0;
	virtual void OnNgHttp2ConnectionClosed() noexcept = 0;
};

class ClientConnection final : BufferedSocketHandler {
	const std::unique_ptr<FilteredSocket> socket;

	ConnectionHandler &handler;

	NgHttp2::Session session;

	class Request;
	using RequestList =
		boost::intrusive::list<Request,
				       boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
				       boost::intrusive::constant_time_size<false>>;

	RequestList requests;

	DeferEvent defer_invoke_idle;

public:
	ClientConnection(EventLoop &loop,
			 UniqueSocketDescriptor fd, FdType fd_type,
			 SocketFilterPtr filter,
			 ConnectionHandler &_handler);

	~ClientConnection() noexcept;

	auto &GetEventLoop() const noexcept {
		return defer_invoke_idle.GetEventLoop();
	}

	bool IsIdle() const noexcept {
		return requests.empty();
	}

	void SendRequest(struct pool &request_pool,
			 StopwatchPtr stopwatch,
			 http_method_t method, const char *uri,
			 StringMap &&headers,
			 UnusedIstreamPtr body,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept;

private:
	void InvokeIdle() noexcept {
		handler.OnNgHttp2ConnectionIdle();
	}

	void RemoveRequest(Request &request) noexcept;

	void AbortAllRequests(std::exception_ptr e) noexcept;

	ssize_t SendCallback(const void *data, size_t length) noexcept;

	static ssize_t SendCallback(nghttp2_session *, const uint8_t *data,
				    size_t length, int,
				    void *user_data) noexcept {
		auto &c = *(ClientConnection *)user_data;
		return c.SendCallback(data, length);
	}

	int OnFrameRecvCallback(const nghttp2_frame *frame) noexcept;

	static int OnFrameRecvCallback(nghttp2_session *,
				       const nghttp2_frame *frame,
				       void *user_data) noexcept {
		auto &c = *(ClientConnection *)user_data;
		return c.OnFrameRecvCallback(frame);
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	// TODO bool OnBufferedDrained() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};

} // namespace NgHttp2
