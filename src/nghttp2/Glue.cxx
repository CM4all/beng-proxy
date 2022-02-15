/*
 * Copyright 2007-2021 CM4all GmbH
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
#include "http/Address.hxx"
#include "http/PendingRequest.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "http/ResponseHandler.hxx"
#include "stopwatch.hxx"

namespace NgHttp2 {

class GlueRequest final : Cancellable, StockGetHandler {
	struct pool &pool;
	AlpnHandler *const alpn_handler;
	HttpResponseHandler &handler;

	const StopwatchPtr stopwatch;

	SocketFilterFactory *const filter_factory;

	const HttpAddress &address;
	PendingHttpRequest pending_request;

	CancellablePointer &caller_cancel_ptr;
	CancellablePointer cancel_ptr;

public:
	GlueRequest(struct pool &_pool, AlpnHandler *_alpn_handler,
		    HttpResponseHandler &_handler,
		    const StopwatchPtr &parent_stopwatch,
		    SocketFilterFactory *_filter_factory,
		    http_method_t _method,
		    const HttpAddress &_address,
		    StringMap &&_headers, UnusedIstreamPtr _body,
		    CancellablePointer &_caller_cancel_ptr) noexcept
		:pool(_pool), alpn_handler(_alpn_handler), handler(_handler),
		 stopwatch(parent_stopwatch, "nghttp2_client"),
		 filter_factory(_filter_factory),
		 address(_address),
		 pending_request(_pool, _method, _address.path,
				 std::move(_headers), std::move(_body)),
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
		if (alpn_handler != nullptr)
			alpn_handler->OnAlpnNoMismatch();

		if (address.host_and_port != nullptr)
			pending_request.headers.Add(pool, "host",
						    address.host_and_port);

		auto &_pool = pool;
		auto _stopwatch = std::move(stopwatch);
		auto &_handler = handler;
		auto request = std::move(pending_request);
		auto &_caller_cancel_ptr = caller_cancel_ptr;
		Destroy();
		connection.SendRequest(_pool, std::move(_stopwatch),
				       request.method, request.uri,
				       std::move(request.headers),
				       std::move(request.body),
				       _handler, _caller_cancel_ptr);
	}

	void OnNgHttp2StockAlpn(std::unique_ptr<FilteredSocket> &&socket) noexcept override;

	void OnNgHttp2StockError(std::exception_ptr e) noexcept override {
		if (alpn_handler != nullptr)
			alpn_handler->OnAlpnError();

		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(std::move(e));
	}
};

void
GlueRequest::OnNgHttp2StockAlpn(std::unique_ptr<FilteredSocket> &&socket) noexcept
{
	if (alpn_handler != nullptr) {
		auto &_alpn_handler = *alpn_handler;
		const SocketAddress _address = *address.addresses.begin(); // TODO
		auto request = std::move(pending_request);
		Destroy();
		_alpn_handler.OnAlpnMismatch(std::move(request), _address,
					     std::move(socket));
		return;
	}

	(void)socket;

	OnNgHttp2StockError(std::make_exception_ptr(std::runtime_error("Server does not support HTTP/2")));
}

void
SendRequest(struct pool &pool, EventLoop &event_loop, Stock &stock,
	    const StopwatchPtr &parent_stopwatch,
	    SocketFilterFactory *filter_factory,
	    http_method_t method,
	    const HttpAddress &address,
	    StringMap &&headers, UnusedIstreamPtr body,
	    AlpnHandler *alpn_handler,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr) noexcept
{
	auto *request = NewFromPool<GlueRequest>(pool, pool,
						 alpn_handler, handler,
						 parent_stopwatch,
						 filter_factory,
						 method, address,
						 std::move(headers),
						 std::move(body),
						 cancel_ptr);
	request->Start(pool, stock, event_loop);
}

} // namespace NgHttp2
