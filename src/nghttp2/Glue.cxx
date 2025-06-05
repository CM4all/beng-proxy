// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Glue.hxx"
#include "Stock.hxx"
#include "Client.hxx"
#include "http/Address.hxx"
#include "http/CommonHeaders.hxx"
#include "http/PendingRequest.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "http/ResponseHandler.hxx"
#include "stopwatch.hxx"

namespace NgHttp2 {

class GlueRequest final : Cancellable, StockGetHandler {
	const AllocatorPtr alloc;
	AlpnHandler *const alpn_handler;
	HttpResponseHandler &handler;

	const StopwatchPtr stopwatch;

	const SocketFilterParams *const filter_params;

	const HttpAddress &address;
	PendingHttpRequest pending_request;

	CancellablePointer &caller_cancel_ptr;
	CancellablePointer cancel_ptr;

public:
	GlueRequest(AllocatorPtr _alloc, AlpnHandler *_alpn_handler,
		    HttpResponseHandler &_handler,
		    const StopwatchPtr &parent_stopwatch,
		    const SocketFilterParams *_filter_params,
		    HttpMethod _method,
		    const HttpAddress &_address,
		    StringMap &&_headers, UnusedIstreamPtr _body,
		    CancellablePointer &_caller_cancel_ptr) noexcept
		:alloc(_alloc), alpn_handler(_alpn_handler), handler(_handler),
		 stopwatch(parent_stopwatch, "nghttp2_client"),
		 filter_params(_filter_params),
		 address(_address),
		 pending_request(alloc.GetPool(), _method, _address.path,
				 std::move(_headers), std::move(_body)),
		 caller_cancel_ptr(_caller_cancel_ptr)
	{
		caller_cancel_ptr = *this;
	}

	void Start(Stock &stock, EventLoop &event_loop) noexcept {
		stock.Get(event_loop, alloc, stopwatch, {},
			  nullptr,
			  *address.addresses.begin(), // TODO
			  std::chrono::seconds(30),
			  filter_params,
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
			pending_request.headers.Add(alloc, host_header,
						    address.host_and_port);

		const auto _alloc = alloc;
		auto _stopwatch = std::move(stopwatch);
		auto &_handler = handler;
		auto request = std::move(pending_request);
		auto &_caller_cancel_ptr = caller_cancel_ptr;
		Destroy();
		connection.SendRequest(_alloc, std::move(_stopwatch),
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

	OnNgHttp2StockError(std::make_exception_ptr(std::runtime_error("Server does not support HTTP/2")));
}

void
SendRequest(AllocatorPtr alloc, EventLoop &event_loop, Stock &stock,
	    const StopwatchPtr &parent_stopwatch,
	    const SocketFilterParams *filter_params,
	    HttpMethod method,
	    const HttpAddress &address,
	    StringMap &&headers, UnusedIstreamPtr body,
	    AlpnHandler *alpn_handler,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr) noexcept
{
	auto *request = alloc.New<GlueRequest>(alloc,
					       alpn_handler, handler,
					       parent_stopwatch,
					       filter_params,
					       method, address,
					       std::move(headers),
					       std::move(body),
					       cancel_ptr);
	request->Start(stock, event_loop);
}

} // namespace NgHttp2
