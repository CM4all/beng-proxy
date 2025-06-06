// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "GlueClient.hxx"
#include "PendingRequest.hxx"
#include "Client.hxx"
#include "Address.hxx"
#include "ResponseHandler.hxx"
#include "HeaderWriter.hxx"
#include "fs/Balancer.hxx"
#include "fs/Handler.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "event/Loop.hxx"
#include "net/SocketAddress.hxx"
#include "net/FailureRef.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"
#include "memory/GrowingBuffer.hxx"

static constexpr Event::Duration HTTP_CONNECT_TIMEOUT =
	std::chrono::seconds(30);

class HttpRequest final
	: Cancellable, FilteredSocketBalancerHandler, HttpResponseHandler, PoolLeakDetector {

	struct pool &pool;

	EventLoop &event_loop;

	FilteredSocketBalancer &fs_balancer;

	StopwatchPtr stopwatch;

	const SocketFilterParams *const filter_params;

	FailurePtr failure;

	const sticky_hash_t sticky_hash;

	unsigned retries;

	const HttpAddress &address;

	PendingHttpRequest pending_request;

	HttpResponseHandler &handler;
	CancellablePointer cancel_ptr;

public:
	HttpRequest(struct pool &_pool, EventLoop &_event_loop,
		    FilteredSocketBalancer &_fs_balancer,
		    const StopwatchPtr &parent_stopwatch,
		    sticky_hash_t _sticky_hash,
		    const SocketFilterParams *_filter_params,
		    HttpMethod _method,
		    const HttpAddress &_address,
		    StringMap &&_headers,
		    UnusedIstreamPtr _body,
		    HttpResponseHandler &_handler,
		    CancellablePointer &_cancel_ptr) noexcept
		:PoolLeakDetector(_pool),
		 pool(_pool), event_loop(_event_loop), fs_balancer(_fs_balancer),
		 stopwatch(parent_stopwatch, _address.path),
		 filter_params(_filter_params),
		 sticky_hash(_sticky_hash),
		 /* can only retry if there is no request body */
		 retries(_body ? 0 : 2),
		 address(_address),
		 pending_request(_pool, _method, _address.path,
				 std::move(_headers), std::move(_body)),
		 handler(_handler)
	{
		_cancel_ptr = *this;
	}

	void BeginConnect() noexcept {
		fs_balancer.Get(pool, stopwatch,
				0, false, SocketAddress::Null(),
				sticky_hash,
				address.addresses,
				HTTP_CONNECT_TIMEOUT,
				filter_params,
				*this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	void Failed(std::exception_ptr ep) noexcept {
		pending_request.Discard();
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(ep);
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class FilteredSocketBalancerHandler */
	void OnFilteredSocketReady(Lease &lease,
				   FilteredSocket &socket,
				   SocketAddress address, const char *name,
				   ReferencedFailureInfo &failure) noexcept override;
	void OnFilteredSocketError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

/*
 * HTTP response handler
 *
 */

void
HttpRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
			    UnusedIstreamPtr _body) noexcept
{
	failure->UnsetProtocol();

	auto &_handler = handler;
	Destroy();
	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

void
HttpRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	if (retries > 0 && IsHttpClientRetryFailure(ep)) {
		/* the server has closed the connection prematurely, maybe
		   because it didn't want to get any further requests on that
		   TCP connection.  Let's try again. */

		--retries;
		BeginConnect();
	} else {
		if (IsHttpClientServerFailure(ep)) {
			failure->SetProtocol(event_loop.SteadyNow(),
					     std::chrono::seconds(20));
		}

		Failed(ep);
	}
}

/*
 * stock callback
 *
 */

void
HttpRequest::OnFilteredSocketReady(Lease &lease,
				   FilteredSocket &socket,
				   SocketAddress, const char *name,
				   ReferencedFailureInfo &_failure) noexcept
{
	stopwatch.RecordEvent("connect");

	failure = _failure;

	GrowingBuffer more_headers;
	if (address.host_and_port != nullptr)
		header_write(more_headers, "host", address.host_and_port);

	http_client_request(pool, std::move(stopwatch),
			    socket, lease, name,
			    pending_request.method, pending_request.uri,
			    pending_request.headers, std::move(more_headers),
			    std::move(pending_request.body), true,
			    *this, cancel_ptr);
}

void
HttpRequest::OnFilteredSocketError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("connect_error");

	Failed(ep);
}

/*
 * constructor
 *
 */

void
http_request(struct pool &pool, EventLoop &event_loop,
	     FilteredSocketBalancer &fs_balancer,
	     const StopwatchPtr &parent_stopwatch,
	     sticky_hash_t sticky_hash,
	     const SocketFilterParams *filter_params,
	     HttpMethod method,
	     const HttpAddress &uwa,
	     StringMap &&headers,
	     UnusedIstreamPtr body,
	     HttpResponseHandler &handler,
	     CancellablePointer &_cancel_ptr) noexcept
{
	assert(uwa.host_and_port != nullptr);
	assert(uwa.path != nullptr);

	auto hr = NewFromPool<HttpRequest>(pool, pool, event_loop, fs_balancer,
					   parent_stopwatch,
					   sticky_hash,
					   filter_params,
					   method, uwa,
					   std::move(headers), std::move(body),
					   handler, _cancel_ptr);

	hr->BeginConnect();
}
