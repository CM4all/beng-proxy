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

#include "BufferedResourceLoader.hxx"
#include "HttpResponseHandler.hxx"
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
	const sticky_hash_t sticky_hash;
	const char *const cache_tag;
	const char *const site_name;
	const http_method_t method;
	const ResourceAddress &address;
	const http_status_t status;
	StringMap headers;
	const char *const body_etag;
	HttpResponseHandler &handler;

	CancellablePointer &caller_cancel_ptr;

public:
	PostponedRequest(struct pool &_pool, ResourceLoader &_next,
			 const StopwatchPtr &_parent_stopwatch,
			 sticky_hash_t _sticky_hash,
			 const char *_cache_tag,
			 const char *_site_name,
			 http_method_t _method,
			 const ResourceAddress &_address,
			 http_status_t _status, StringMap &&_headers,
			 const char *_body_etag,
			 HttpResponseHandler &_handler,
			 CancellablePointer &_caller_cancel_ptr) noexcept
		:pool(_pool),
		 next(_next), parent_stopwatch(_parent_stopwatch),
		 sticky_hash(_sticky_hash),
		 cache_tag(_cache_tag),
		 site_name(_site_name),
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
		next.SendRequest(pool, parent_stopwatch,
				 sticky_hash, cache_tag, site_name,
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
		sticky_hash_t _sticky_hash,
		const char *_cache_tag,
		const char *_site_name,
		http_method_t _method, const ResourceAddress &_address,
		http_status_t _status, StringMap &&_headers,
		const char *_body_etag,
		HttpResponseHandler &_handler,
		CancellablePointer &caller_cancel_ptr) noexcept
		:PoolLeakDetector(_pool),
		 postponed_request(_pool, _next,
				   _parent_stopwatch,
				   _sticky_hash,
				   _cache_tag, _site_name,
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
				    sticky_hash_t sticky_hash,
				    const char *cache_tag,
				    const char *site_name,
				    http_method_t method,
				    const ResourceAddress &address,
				    http_status_t status, StringMap &&headers,
				    UnusedIstreamPtr body, const char *body_etag,
				    HttpResponseHandler &handler,
				    CancellablePointer &cancel_ptr) noexcept
{
	if (body) {
		auto *request = NewFromPool<Request>(pool, pool, next, parent_stopwatch,
						     sticky_hash,
						     cache_tag, site_name,
						     method, address,
						     status, std::move(headers),
						     body_etag, handler, cancel_ptr);
		request->Start(event_loop, pipe_stock, std::move(body));
	} else {
		next.SendRequest(pool, parent_stopwatch,
				 sticky_hash, cache_tag, site_name,
				 method, address, status, std::move(headers),
				 std::move(body), body_etag,
				 handler, cancel_ptr);
	}
}
