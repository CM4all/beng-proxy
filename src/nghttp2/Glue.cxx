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

#include "Glue.hxx"
#include "Stock.hxx"
#include "Client.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "http/Address.hxx"
#include "AllocatorPtr.hxx"
#include "HttpResponseHandler.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"

namespace NgHttp2 {

class GlueRequest final : Cancellable, StockGetHandler {
	struct pool &pool;
	HttpResponseHandler &handler;

	const StopwatchPtr stopwatch;

	SocketFilterFactory *const filter_factory;

	const http_method_t method;
	const HttpAddress &address;
	StringMap headers;
	UnusedHoldIstreamPtr body;

	CancellablePointer &caller_cancel_ptr;
	CancellablePointer cancel_ptr;

public:
	GlueRequest(struct pool &_pool, HttpResponseHandler &_handler,
		    const StopwatchPtr &parent_stopwatch,
		    SocketFilterFactory *_filter_factory,
		    http_method_t _method,
		    const HttpAddress &_address,
		    StringMap &&_headers, UnusedIstreamPtr _body,
		    CancellablePointer &_caller_cancel_ptr) noexcept
		:pool(_pool), handler(_handler),
		 stopwatch(parent_stopwatch, "nghttp2_client"),
		 filter_factory(_filter_factory),
		 method(_method), address(_address),
		 headers(std::move(_headers)), body(pool, std::move(_body)),
		 caller_cancel_ptr(_caller_cancel_ptr)
	{
		caller_cancel_ptr = *this;
	}

	void Start(AllocatorPtr alloc,
		   Stock &stock, EventLoop &event_loop) noexcept {
		stock.Get(event_loop, alloc, stopwatch, nullptr,
			  nullptr,
			  *address.addresses.begin(), // TODO
			  std::chrono::seconds(30),
			  filter_factory,
			  *this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		this->~GlueRequest();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class StockGetHandler */
	void OnNgHttp2StockReady(ClientConnection &connection) noexcept override {
		auto &_pool = pool;
		auto _stopwatch = std::move(stopwatch);
		auto &_handler = handler;
		const auto _method = method;
		const auto &_address = address;
		auto _headers = std::move(headers);
		auto _body = std::move(body);
		auto &_caller_cancel_ptr = caller_cancel_ptr;
		Destroy();
		connection.SendRequest(_pool, std::move(_stopwatch),
				       _method, _address.path,
				       std::move(_headers),
				       std::move(_body),
				       _handler, _caller_cancel_ptr);
	}

	void OnNgHttp2StockError(std::exception_ptr e) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(std::move(e));
	}
};

void
SendRequest(struct pool &pool, EventLoop &event_loop, Stock &stock,
	    const StopwatchPtr &parent_stopwatch,
	    SocketFilterFactory *filter_factory,
	    http_method_t method,
	    const HttpAddress &address,
	    StringMap &&headers, UnusedIstreamPtr body,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr) noexcept
{
	auto *request = NewFromPool<GlueRequest>(pool, pool, handler,
						 parent_stopwatch,
						 filter_factory,
						 method, address,
						 std::move(headers),
						 std::move(body),
						 cancel_ptr);
	request->Start(pool, stock, event_loop);
}

} // namespace NgHttp2
