// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "BufferedResourceLoader.hxx"
#include "http/ResponseHandler.hxx"
#include "strmap.hxx"
#include "istream/BufferedIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

namespace {

class PostponedRequest {
	struct pool &pool;
	ResourceLoader &next;
	const StopwatchPtr parent_stopwatch;
	const ResourceRequestParams params;
	const HttpMethod method;
	const ResourceAddress &address;
	const HttpStatus status;
	StringMap headers;
	const char *const body_etag;
	HttpResponseHandler &handler;

	CancellablePointer &caller_cancel_ptr;

public:
	PostponedRequest(struct pool &_pool, ResourceLoader &_next,
			 const StopwatchPtr &_parent_stopwatch,
			 const ResourceRequestParams &_params,
			 HttpMethod _method,
			 const ResourceAddress &_address,
			 HttpStatus _status, StringMap &&_headers,
			 const char *_body_etag,
			 HttpResponseHandler &_handler,
			 CancellablePointer &_caller_cancel_ptr) noexcept
		:pool(_pool),
		 next(_next), parent_stopwatch(_parent_stopwatch),
		 params(_params),
		 method(_method), address(_address),
		 status(_status),
		 /* copy the headers, because they may come from a
		    FilterCacheRequest pool which may be freed before
		    BufferedIstream becomes ready */
		 headers(pool, _headers),
		 body_etag(_body_etag),
		 handler(_handler), caller_cancel_ptr(_caller_cancel_ptr)
	{
	}

	auto &GetPool() const noexcept {
		return pool;
	}

	auto &GetHandler() noexcept {
		return handler;
	}

	void Send(UnusedIstreamPtr body) noexcept {
		next.SendRequest(pool, parent_stopwatch, params,
				 method, address, status, std::move(headers),
				 std::move(body), body_etag,
				 handler, caller_cancel_ptr);
	}
};

}

class BufferedResourceLoader::Request final
	: PoolLeakDetector, Cancellable, BufferedIstreamHandler
{
	PostponedRequest postponed_request;

	CancellablePointer cancel_ptr;

public:
	Request(struct pool &_pool, ResourceLoader &_next,
		const StopwatchPtr &_parent_stopwatch,
		const ResourceRequestParams &_params,
		HttpMethod _method, const ResourceAddress &_address,
		HttpStatus _status, StringMap &&_headers,
		const char *_body_etag,
		HttpResponseHandler &_handler,
		CancellablePointer &caller_cancel_ptr) noexcept
		:PoolLeakDetector(_pool),
		 postponed_request(_pool, _next,
				   _parent_stopwatch, _params,
				   _method, _address,
				   _status, std::move(_headers),
				   _body_etag, _handler, caller_cancel_ptr)
	{
		caller_cancel_ptr = *this;
	}

	auto &GetPool() const noexcept {
		return postponed_request.GetPool();
	}

	void Start(EventLoop &_event_loop, PipeStock *_pipe_stock,
		   UnusedIstreamPtr &&body) noexcept {
		NewBufferedIstream(GetPool(),
				   _event_loop, _pipe_stock,
				   *this, std::move(body),
				   cancel_ptr);
	}

private:
	void Destroy() noexcept {
		DeleteFromPool(GetPool(), this);
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class BufferedIstreamHandler */
	void OnBufferedIstreamReady(UnusedIstreamPtr i) noexcept override {
		auto _pr = std::move(postponed_request);
		Destroy();
		_pr.Send(std::move(i));
	}

	void OnBufferedIstreamError(std::exception_ptr e) noexcept override {
		auto &_handler = postponed_request.GetHandler();
		Destroy();
		_handler.InvokeError(std::move(e));
	}
};

void
BufferedResourceLoader::SendRequest(struct pool &pool,
				    const StopwatchPtr &parent_stopwatch,
				    const ResourceRequestParams &params,
				    HttpMethod method,
				    const ResourceAddress &address,
				    HttpStatus status, StringMap &&headers,
				    UnusedIstreamPtr body, const char *body_etag,
				    HttpResponseHandler &handler,
				    CancellablePointer &cancel_ptr) noexcept
{
	if (body) {
		auto *request = NewFromPool<Request>(pool, pool, next, parent_stopwatch,
						     params,
						     method, address,
						     status, std::move(headers),
						     body_etag, handler, cancel_ptr);
		request->Start(event_loop, pipe_stock, std::move(body));
	} else {
		next.SendRequest(pool, parent_stopwatch,
				 params,
				 method, address, status, std::move(headers),
				 std::move(body), body_etag,
				 handler, cancel_ptr);
	}
}
