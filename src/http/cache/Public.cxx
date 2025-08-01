// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Internal.hxx"
#include "Document.hxx"
#include "Item.hxx"
#include "RFC.hxx"
#include "Heap.hxx"
#include "strmap.hxx"
#include "http/ResponseHandler.hxx"
#include "http/rl/ResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "memory/sink_rubber.hxx"
#include "stats/CacheStats.hxx"
#include "http/CommonHeaders.hxx"
#include "http/Date.hxx"
#include "http/List.hxx"
#include "http/Method.hxx"
#include "http/PDigestHeader.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/TeeIstream.hxx"
#include "istream/RefIstream.hxx"
#include "pool/Holder.hxx"
#include "AllocatorPtr.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "event/FarTimerEvent.hxx"
#include "event/Loop.hxx"
#include "io/Logger.hxx"
#include "util/Base32.hxx"
#include "util/Cancellable.hxx"
#include "util/djb_hash.hxx"
#include "util/Exception.hxx"
#include "util/IntrusiveList.hxx"
#include "util/StringAPI.hxx"

#include <functional>

#include <string.h>
#include <stdio.h>

using std::string_view_literals::operator""sv;

static constexpr Event::Duration http_cache_compress_interval = std::chrono::minutes(10);

static constexpr bool
IsModifyingMethod(HttpMethod method) noexcept
{
	return !IsSafeMethod(method);
}

class HttpCacheRequest final : PoolHolder,
			       HttpResponseHandler,
			       RubberSinkHandler,
			       Cancellable {
public:
	IntrusiveListHook<IntrusiveHookMode::NORMAL> siblings;

private:
	PoolPtr caller_pool;

	const char *const cache_tag;

	/**
	 * The cache object which got this request.
	 */
	HttpCache &cache;

	/**
	 * The cache key used to address the associated cache document.
	 */
	const StringWithHash key;

	/** headers from the original request */
	const StringMap request_headers;

	HttpResponseHandler &handler;

	const HttpCacheRequestInfo request_info;

	/**
	 * Information on the request passed to http_cache_request().
	 */
	HttpCacheResponseInfo info;

	/**
	 * The document which was found in the cache, in case this is a
	 * request to test the validity of the cache entry.  If this is
	 * nullptr, then we had a cache miss.
	 */
	HttpCacheDocument *const document;

	/**
	 * A lease for #document.
	 */
	const SharedLease lease;

	/**
	 * This struct holds response information while this module
	 * receives the response body.
	 */
	struct {
		HttpStatus status;
		StringMap *headers;
	} response;

	CancellablePointer cancel_ptr;

	const bool eager_cache;

public:
	HttpCacheRequest(PoolPtr &&_pool, struct pool &_caller_pool,
			 bool _eager_cache,
			 const char *_cache_tag,
			 HttpCache &_cache,
			 StringWithHash _key,
			 const StringMap &_headers,
			 HttpResponseHandler &_handler,
			 const HttpCacheRequestInfo &_info,
			 HttpCacheDocument *_document,
			 SharedLease &&_lease) noexcept;

	HttpCacheRequest(const HttpCacheRequest &) = delete;
	HttpCacheRequest &operator=(const HttpCacheRequest &) = delete;

	using PoolHolder::GetPool;

	StringWithHash GetKey() const noexcept {
		return key;
	}

	void Start(ResourceLoader &next,
		   const StopwatchPtr &parent_stopwatch,
		   const ResourceRequestParams &params,
		   HttpMethod method,
		   const ResourceAddress &address,
		   StringMap &&_headers,
		   CancellablePointer &_cancel_ptr) noexcept {
		_cancel_ptr = *this;

		next.SendRequest(GetPool(), parent_stopwatch,
				 params,
				 method, address,
				 std::move(_headers), nullptr,
				 *this, cancel_ptr);
	}

	EventLoop &GetEventLoop() const noexcept;

	void Serve() noexcept;

	void Put(RubberAllocation &&a, size_t size) noexcept;

	/**
	 * Storing the response body in the rubber allocator has finished
	 * (but may have failed).
	 */
	void RubberStoreFinished() noexcept;

	/**
	 * Abort storing the response body in the rubber allocator.
	 *
	 * This will not remove the request from the HttpCache, because
	 * this method is supposed to be used as a "disposer".
	 */
	void AbortRubberStore() noexcept;

private:
	void Destroy() noexcept {
		this->~HttpCacheRequest();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class RubberSinkHandler */
	void RubberDone(RubberAllocation &&a, size_t size) noexcept override;
	void RubberOutOfMemory() noexcept override;
	void RubberTooLarge() noexcept override;
	void RubberError(std::exception_ptr ep) noexcept override;
};

/**
 * Wrapper for a uncacheable request which implements #AUTO_FLUSH_CACHE.
 */
class AutoFlushHttpCacheRequest final
	: public HttpResponseHandler, Cancellable
{
	const char *const cache_tag;

	/**
	 * The cache object which got this request.
	 */
	HttpCache &cache;

	HttpResponseHandler &handler;

	CancellablePointer cancel_ptr;

public:
	AutoFlushHttpCacheRequest(const char *_cache_tag,
				  HttpCache &_cache,
				  HttpResponseHandler &_handler) noexcept
		:cache_tag(_cache_tag),
		 cache(_cache),
		 handler(_handler) {}

	AutoFlushHttpCacheRequest(const AutoFlushHttpCacheRequest &) = delete;
	AutoFlushHttpCacheRequest &operator=(const AutoFlushHttpCacheRequest &) = delete;

	void Start(ResourceLoader &next, struct pool &pool,
		   const StopwatchPtr &parent_stopwatch,
		   const ResourceRequestParams &params,
		   HttpMethod method,
		   const ResourceAddress &address,
		   StringMap &&_headers, UnusedIstreamPtr body,
		   CancellablePointer &_cancel_ptr) noexcept {
		_cancel_ptr = *this;

		next.SendRequest(pool, parent_stopwatch,
				 params,
				 method, address,
				 std::move(_headers), std::move(body),
				 *this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		this->~AutoFlushHttpCacheRequest();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;

	void OnHttpError(std::exception_ptr e) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(std::move(e));
	}
};

class HttpCache {
	const PoolPtr pool;

	EventLoop &event_loop;

	FarTimerEvent compress_timer;

	HttpCacheHeap heap;

	ResourceLoader &resource_loader;

	/**
	 * A list of requests that are currently saving their contents to
	 * the cache.
	 */
	IntrusiveList<HttpCacheRequest,
		      IntrusiveListMemberHookTraits<&HttpCacheRequest::siblings>> requests;

	mutable CacheStats stats{};

	const bool obey_no_cache;

public:
	HttpCache(struct pool &_pool, size_t max_size,
		  bool obey_no_cache,
		  EventLoop &event_loop,
		  ResourceLoader &_resource_loader);

	HttpCache(const HttpCache &) = delete;
	HttpCache &operator=(const HttpCache &) = delete;

	~HttpCache() noexcept;

	EventLoop &GetEventLoop() const noexcept {
		return event_loop;
	}

	Rubber &GetRubber() noexcept {
		return heap.GetRubber();
	}

	void ForkCow(bool inherit) noexcept {
		heap.ForkCow(inherit);
	}

	CacheStats GetStats() const noexcept {
		stats.allocator = heap.GetStats();
		return stats;
	}

	void Flush() noexcept {
		heap.Flush();
	}

	void FlushTag(std::string_view tag) noexcept {
		heap.FlushTag(tag);
	}

	void AddRequest(HttpCacheRequest &r) noexcept {
		requests.push_front(r);
	}

	void RemoveRequest(HttpCacheRequest &r) noexcept {
		requests.erase(requests.iterator_to(r));
	}

	void Start(struct pool &caller_pool,
		   const StopwatchPtr &parent_stopwatch,
		   const ResourceRequestParams &params,
		   HttpMethod method,
		   const ResourceAddress &address,
		   StringMap &&headers, UnusedIstreamPtr body,
		   HttpResponseHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;

	void Put(StringWithHash key, const char *tag,
		 const HttpCacheResponseInfo &info,
		 const StringMap &request_headers,
		 HttpStatus status,
		 const StringMap &response_headers,
		 RubberAllocation &&a, size_t size) noexcept {
		LogConcat(4, "HttpCache", "put ", key.value);
		++stats.stores;

		heap.Put(key, tag, info, request_headers,
			 status, response_headers,
			 std::move(a), size);
	}

	void Remove(HttpCacheDocument *document) noexcept {
		heap.Remove(*document);
	}

	void Remove(StringWithHash key, StringMap &headers) noexcept {
		heap.Remove(key, headers);
	}

	[[nodiscard]]
	SharedLease Lock(HttpCacheDocument &document) noexcept {
		return heap.Lock(document);
	}

	/**
	 * Query the cache.
	 *
	 * Caller pool is referenced synchronously and freed
	 * asynchronously (as needed).
	 */
	void Use(struct pool &caller_pool,
		 const StopwatchPtr &parent_stopwatch,
		 StringWithHash key,
		 const ResourceRequestParams &params,
		 HttpMethod method,
		 const ResourceAddress &address,
		 StringMap &&headers,
		 const HttpCacheRequestInfo &info,
		 HttpResponseHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Send the cached document to the caller.
	 *
	 * Caller pool is left unchanged.
	 */
	void Serve(struct pool &caller_pool,
		   HttpCacheDocument &document,
		   StringWithHash key,
		   HttpResponseHandler &handler) noexcept;

private:
	/**
	 * A resource was not found in the cache.
	 *
	 * Caller pool is referenced synchronously and freed asynchronously.
	 */
	void Miss(struct pool &caller_pool,
		  const StopwatchPtr &parent_stopwatch,
		  StringWithHash key,
		  const ResourceRequestParams &params,
		  const HttpCacheRequestInfo &info,
		  HttpMethod method,
		  const ResourceAddress &address,
		  StringMap &&headers,
		  HttpResponseHandler &handler,
		  CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Revalidate a cache entry.
	 *
	 * Caller pool is referenced synchronously and freed asynchronously.
	 */
	void Revalidate(struct pool &caller_pool,
			const StopwatchPtr &parent_stopwatch,
			StringWithHash key,
			const ResourceRequestParams &params,
			const HttpCacheRequestInfo &info,
			HttpCacheDocument &document,
			HttpMethod method,
			const ResourceAddress &address,
			StringMap &&headers,
			HttpResponseHandler &handler,
			CancellablePointer &cancel_ptr) noexcept;

	/**
	 * The requested document was found in the cache.  It is either
	 * served or revalidated.
	 *
	 * Caller pool is referenced synchronously and freed asynchronously
	 * (as needed).
	 */
	void Found(const HttpCacheRequestInfo &info,
		   HttpCacheDocument &document,
		   StringWithHash key,
		   struct pool &caller_pool,
		   const StopwatchPtr &parent_stopwatch,
		   const ResourceRequestParams &params,
		   HttpMethod method,
		   const ResourceAddress &address,
		   StringMap &&headers,
		   HttpResponseHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;

	void OnCompressTimer() noexcept {
		heap.Compress();
		compress_timer.Schedule(http_cache_compress_interval);
	}
};

static void
UpdateHeader(AllocatorPtr alloc, StringMap &dest,
	     const StringMap &src, const char *name) noexcept
{
	const char *value = src.Get(name);
	if (value != nullptr)
		dest.SecureSet(alloc, name, alloc.Dup(value));
}

static StringWithHash
http_cache_key(const AllocatorPtr alloc, const ResourceAddress &address,
	       StringWithHash id) noexcept
{
	switch (address.type) {
	case ResourceAddress::Type::NONE:
	case ResourceAddress::Type::LOCAL:
	case ResourceAddress::Type::PIPE:
		/* not cacheable */
		return StringWithHash{nullptr};

	case ResourceAddress::Type::HTTP:
	case ResourceAddress::Type::LHTTP:
	case ResourceAddress::Type::CGI:
	case ResourceAddress::Type::FASTCGI:
	case ResourceAddress::Type::WAS:
		// TODO optimize hasher
		return id.IsNull()
			? address.GetId(alloc)
		       : id;
	}

	/* unreachable */
	assert(false);
	return StringWithHash{nullptr};
}

inline EventLoop &
HttpCacheRequest::GetEventLoop() const noexcept
{
	return cache.GetEventLoop();
}

void
HttpCacheRequest::Put(RubberAllocation &&a, size_t size) noexcept
{
	cache.Put(key, cache_tag, info, request_headers,
		  response.status, *response.headers,
		  std::move(a), size);
}

/*
 * sink_rubber handler
 *
 */

[[gnu::pure]]
static std::span<const std::byte>
ToSpan(const RubberAllocation &allocation, std::size_t size) noexcept
{
	if (size == 0)
		/* this needs to be a special case because it is not
                   allowed to call Read() on an empty
                   RubberAllocation */
		return {};

	return {
		reinterpret_cast<const std::byte *>(allocation.Read()),
		size,
	};
}

void
HttpCacheRequest::RubberDone(RubberAllocation &&a, size_t size) noexcept
{
	RubberStoreFinished();

	if (eager_cache && !response.headers->Contains(digest_header)) {
		const AllocatorPtr alloc{GetPool()};
		response.headers->Add(alloc, digest_header, GenerateDigestHeader(alloc, ToSpan(a, size)));
	}

	/* the request was successful, and all of the body data has been
	   saved: add it to the cache */
	Put(std::move(a), size);
	Destroy();
}

void
HttpCacheRequest::RubberOutOfMemory() noexcept
{
	LogConcat(4, "HttpCache", "nocache oom ", key.value);

	RubberStoreFinished();
	Destroy();
}

void
HttpCacheRequest::RubberTooLarge() noexcept
{
	LogConcat(4, "HttpCache", "nocache too large ", key.value);

	RubberStoreFinished();
	Destroy();
}

void
HttpCacheRequest::RubberError(std::exception_ptr ep) noexcept
{
	LogConcat(4, "HttpCache", "body_abort ", key.value, ": ", ep);

	RubberStoreFinished();
	Destroy();
}

/*
 * http response handler
 *
 */

void
AutoFlushHttpCacheRequest::OnHttpResponse(HttpStatus status,
					  StringMap &&_headers,
					  UnusedIstreamPtr body) noexcept
{
	assert(cache_tag != nullptr);

	if (!http_status_is_error(status))
		cache.FlushTag(cache_tag);

	auto &_handler = handler;
	Destroy();

	_handler.InvokeResponse(status, std::move(_headers), std::move(body));
}

void
HttpCacheRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
				 UnusedIstreamPtr body) noexcept
{
	const AllocatorPtr alloc{GetPool()};

	if (document != nullptr && status == HttpStatus::NOT_MODIFIED) {
		assert(!body);

		if (auto _info = http_cache_response_evaluate(request_info,
							      alloc,
							      eager_cache,
							      HttpStatus::OK,
							      _headers, -1);
		    _info && _info->expires >= GetEventLoop().SystemNow()) {
			/* copy the new "Expires" (or "max-age") value from the
			   "304 Not Modified" response */
			auto &item = *(HttpCacheItem *)document;
			item.SetExpires(GetEventLoop().SteadyNow(),
					GetEventLoop().SystemNow(),
					_info->expires);

			const AllocatorPtr item_alloc{item.GetPool()};

			/* TODO: this leaks pool memory each time we update
			   headers; how to fix this? */
			UpdateHeader(item_alloc, document->response_headers, _headers, "expires");
			UpdateHeader(item_alloc, document->response_headers, _headers, "cache-control");
		}

		LogConcat(5, "HttpCache", "not_modified ", key.value);
		Serve();
		Destroy();
		return;
	}

	if (document != nullptr &&
	    http_cache_prefer_cached(*document, _headers)) {
		LogConcat(4, "HttpCache", "matching etag '", document->info.etag,
			  "' for ", key.value, ", using cache entry");

		body.Clear();

		Serve();
		Destroy();
		return;
	}

	if (document != nullptr)
		cache.Remove(document);

	const off_t available = body
		? body.GetAvailable(true)
		: 0;

	if (auto _info = http_cache_response_evaluate(request_info, alloc,
						      eager_cache,
						      status, _headers,
						      available);
	    _info) {
		info = std::move(*_info);
	} else {
		/* don't cache response */
		LogConcat(4, "HttpCache", "nocache ", key.value);

		if (body)
			body = NewRefIstream(pool, std::move(body));
		else
			/* workaround: if there is no response body,
			   nobody will hold a pool reference, and the
			   headers will be freed after
			   InvokeResponse() returns; in that case, we
			   need to copy all headers into the caller's
			   pool to avoid use-after-free bugs */
			_headers = {caller_pool, _headers};

		handler.InvokeResponse(status, std::move(_headers), std::move(body));
		Destroy();
		return;
	}

	response.status = status;
	response.headers = strmap_dup(pool, &_headers);

	/* move the caller_pool reference to the stack to ensure it gets
	   unreferenced at the end of this method - not earlier and not
	   later */
	const PoolPtr _caller_pool = std::move(caller_pool);

	/* copy the HttpResponseHandler reference to the stack, because
	   the sink_rubber_new() call may destroy this object */
	auto &_handler = handler;

	/* hold an additional pool reference to ensure that all header
	   strings stay valid until he handler returns, just in case
	   sink_rubber_new() destroys this object and the pool; TODO:
	   find a better solution for this */
	const ScopePoolRef ref(pool);

	bool destroy = false;
	if (!body) {
		Put({}, 0);
		destroy = true;

		/* workaround: if there is no response body, nobody
		   will hold a pool reference, and the headers will be
		   freed after InvokeResponse() returns; in that case,
		   we need to copy all headers into the caller's pool
		   to avoid use-after-free bugs */
		_headers = {_caller_pool, _headers};
	} else {
		/* this->info was allocated from the caller pool; duplicate
		   it to keep it alive even after the caller pool is
		   destroyed */
		info.MoveToPool(alloc);

		/* tee the body: one goes to our client, and one goes into the
		   cache */
		auto tee = NewTeeIstream(pool, std::move(body),
					 GetEventLoop(),
					 false,
					 /* just in case our handler closes
					    the body without looking at it:
					    defer an Istream::Read() call for
					    the Rubber sink */
					 true);

		cache.AddRequest(*this);

		sink_rubber_new(pool, AddTeeIstream(tee, false),
				cache.GetRubber(), cacheable_size_limit,
				*this,
				cancel_ptr);

		body = std::move(tee);
	}

	_handler.InvokeResponse(status, std::move(_headers), std::move(body));

	if (destroy)
		Destroy();
}

void
HttpCacheRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	ep = NestException(ep, FmtRuntimeError("http_cache {}", key.value));

	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(ep);
}

/*
 * async operation
 *
 */

void
HttpCacheRequest::Cancel() noexcept
{
	cancel_ptr.Cancel();
	Destroy();
}


/*
 * constructor and public methods
 *
 */

inline
HttpCacheRequest::HttpCacheRequest(PoolPtr &&_pool,
				   struct pool &_caller_pool,
				   bool _eager_cache,
				   const char *_cache_tag,
				   HttpCache &_cache,
				   StringWithHash _key,
				   const StringMap &_headers,
				   HttpResponseHandler &_handler,
				   const HttpCacheRequestInfo &_request_info,
				   HttpCacheDocument *_document,
				   SharedLease &&_lease) noexcept
	:PoolHolder(std::move(_pool)), caller_pool(_caller_pool),
	 cache_tag(AllocatorPtr{pool}.CheckDup(_cache_tag)),
	 cache(_cache),
	 key(AllocatorPtr{pool}.Dup(_key)),
	 request_headers(pool, _headers),
	 handler(_handler),
	 request_info(_request_info),
	 document(_document), lease(std::move(_lease)),
	 eager_cache(_eager_cache)
{
}

inline
HttpCache::HttpCache(struct pool &_pool, size_t max_size,
		     bool _obey_no_cache,
		     EventLoop &_event_loop,
		     ResourceLoader &_resource_loader)
	:pool(pool_new_dummy(&_pool, "http_cache")),
	 event_loop(_event_loop),
	 compress_timer(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
	 heap(pool, event_loop, max_size),
	 resource_loader(_resource_loader),
	 obey_no_cache(_obey_no_cache)
{
	assert(max_size > 0);

	compress_timer.Schedule(http_cache_compress_interval);
}

HttpCache *
http_cache_new(struct pool &pool, size_t max_size,
	       bool obey_no_cache,
	       EventLoop &event_loop,
	       ResourceLoader &resource_loader)
{
	assert(max_size > 0);

	return new HttpCache(pool, max_size, obey_no_cache,
			     event_loop, resource_loader);
}

void
HttpCacheRequest::RubberStoreFinished() noexcept
{
	assert(cancel_ptr);

	cancel_ptr = nullptr;
	cache.RemoveRequest(*this);
}

void
HttpCacheRequest::AbortRubberStore() noexcept
{
	cancel_ptr.Cancel();
	Destroy();
}

inline
HttpCache::~HttpCache() noexcept
{
	requests.clear_and_dispose(std::mem_fn(&HttpCacheRequest::AbortRubberStore));
}

void
http_cache_close(HttpCache *cache) noexcept
{
	delete cache;
}

void
http_cache_fork_cow(HttpCache &cache, bool inherit) noexcept
{
	cache.ForkCow(inherit);
}

CacheStats
http_cache_get_stats(const HttpCache &cache) noexcept
{
	return cache.GetStats();
}

void
http_cache_flush(HttpCache &cache) noexcept
{
	cache.Flush();
}

void
http_cache_flush_tag(HttpCache &cache, std::string_view tag) noexcept
{
	cache.FlushTag(tag);
}

inline void
HttpCache::Miss(struct pool &caller_pool,
		const StopwatchPtr &parent_stopwatch,
		StringWithHash key,
		const ResourceRequestParams &params,
		const HttpCacheRequestInfo &info,
		HttpMethod method,
		const ResourceAddress &address,
		StringMap &&headers,
		HttpResponseHandler &handler,
		CancellablePointer &cancel_ptr) noexcept
{
	++stats.misses;

	if (info.only_if_cached) {
		/* see RFC 9111 5.2.1.7 */
		handler.InvokeResponse(HttpStatus::GATEWAY_TIMEOUT,
				       {}, UnusedIstreamPtr());
		return;
	}

	/* the cache request may live longer than the caller pool, so
	   allocate a new pool for it from cache.pool */
	auto request_pool = pool_new_linear(pool, "HttpCacheRequest", 8192);

	auto request =
		NewFromPool<HttpCacheRequest>(std::move(request_pool), caller_pool,
					      params.eager_cache,
					      params.cache_tag,
					      *this,
					      key,
					      headers,
					      handler,
					      info, nullptr, SharedLease{});

	LogConcat(4, "HttpCache", "miss ", request->GetKey().value);

	request->Start(resource_loader, parent_stopwatch,
		       params,
		       method, address,
		       std::move(headers),
		       cancel_ptr);
}

[[gnu::pure]]
static bool
CheckETagList(const char *list, const StringMap &response_headers) noexcept
{
	assert(list != nullptr);

	if (StringIsEqual(list, "*"))
		return true;

	const char *etag = response_headers.Get(etag_header);
	return etag != nullptr && http_list_contains(list, etag);
}

static void
DispatchNotModified(struct pool &pool, const HttpCacheDocument &document,
		    HttpResponseHandler &handler) noexcept
{
	handler.InvokeResponse(HttpStatus::NOT_MODIFIED,
			       StringMap(pool, document.response_headers),
			       UnusedIstreamPtr());
}

static bool
CheckCacheRequest(struct pool &pool, const HttpCacheRequestInfo &info,
		  const HttpCacheDocument &document,
		  HttpResponseHandler &handler) noexcept
{
	bool ignore_if_modified_since = false;

	if (info.if_match != nullptr &&
	    !CheckETagList(info.if_match, document.response_headers)) {
		handler.InvokeResponse(HttpStatus::PRECONDITION_FAILED,
				       {}, UnusedIstreamPtr());
		return false;
	}

	if (info.if_none_match != nullptr) {
		if (CheckETagList(info.if_none_match, document.response_headers)) {
			DispatchNotModified(pool, document, handler);
			return false;
		}

		/* RFC 2616 14.26: "If none of the entity tags match, then the
		   server MAY perform the requested method as if the
		   If-None-Match header field did not exist, but MUST also
		   ignore any If-Modified-Since header field(s) in the
		   request." */
		ignore_if_modified_since = true;
	}

	if (info.if_modified_since && !ignore_if_modified_since) {
		const char *last_modified = document.response_headers.Get(last_modified_header);
		if (last_modified != nullptr) {
			if (StringIsEqual(info.if_modified_since, last_modified)) {
				/* common fast path: client sends the previous
				   Last-Modified header string as-is */
				DispatchNotModified(pool, document, handler);
				return false;
			}

			const auto ims = http_date_parse(info.if_modified_since);
			const auto lm = http_date_parse(last_modified);
			if (ims != std::chrono::system_clock::from_time_t(-1) &&
			    lm != std::chrono::system_clock::from_time_t(-1) &&
			    lm <= ims) {
				DispatchNotModified(pool, document, handler);
				return false;
			}
		}
	}

	if (info.if_unmodified_since) {
		const char *last_modified = document.response_headers.Get(last_modified_header);
		if (last_modified != nullptr) {
			const auto iums = http_date_parse(info.if_unmodified_since);
			const auto lm = http_date_parse(last_modified);
			if (iums != std::chrono::system_clock::from_time_t(-1) &&
			    lm != std::chrono::system_clock::from_time_t(-1) &&
			    lm > iums) {
				handler.InvokeResponse(HttpStatus::PRECONDITION_FAILED,
						       {}, UnusedIstreamPtr());
				return false;
			}
		}
	}

	return true;
}

inline void
HttpCache::Serve(struct pool &caller_pool,
		 HttpCacheDocument &document,
		 const StringWithHash key,
		 HttpResponseHandler &handler) noexcept
{
	LogConcat(4, "HttpCache", "serve ", key.value);

	auto body = heap.OpenStream(caller_pool, document);

	StringMap headers = body
		? StringMap{ShallowCopy{}, caller_pool, document.response_headers}
		/* workaround: if there is no response body, nobody
		   will hold a pool reference, and the headers will be
		   freed after InvokeResponse() returns; in that case,
		   we need to copy all headers into the caller's pool
		   to avoid use-after-free bugs */
		: StringMap{caller_pool, document.response_headers};

	handler.InvokeResponse(document.status,
			       std::move(headers),
			       std::move(body));
}

/**
 * Send the cached document to the caller.
 *
 * Caller pool is left unchanged.
 */
inline void
HttpCacheRequest::Serve() noexcept
{
	if (!CheckCacheRequest(pool, request_info, *document, handler))
		return;

	cache.Serve(caller_pool, *document, key, handler);
}

inline void
HttpCache::Revalidate(struct pool &caller_pool,
		      const StopwatchPtr &parent_stopwatch,
		      const StringWithHash key,
		      const ResourceRequestParams &params,
		      const HttpCacheRequestInfo &info,
		      HttpCacheDocument &document,
		      HttpMethod method,
		      const ResourceAddress &address,
		      StringMap &&headers,
		      HttpResponseHandler &handler,
		      CancellablePointer &cancel_ptr) noexcept
{
	/* the cache request may live longer than the caller pool, so
	   allocate a new pool for it from cache.pool */
	auto request_pool = pool_new_linear(pool, "HttpCacheRequest", 8192);

	auto request =
		NewFromPool<HttpCacheRequest>(std::move(request_pool), caller_pool,
					      params.eager_cache,
					      params.cache_tag,
					      *this,
					      key,
					      headers,
					      handler,
					      info, &document, Lock(document));

	LogConcat(4, "HttpCache", "test ", request->GetKey().value);

	if (document.info.last_modified != nullptr)
		headers.Set(request->GetPool(),
			    if_modified_since_header, document.info.last_modified);

	if (document.info.etag != nullptr)
		headers.Set(request->GetPool(),
			    if_none_match_header, document.info.etag);

	request->Start(resource_loader, parent_stopwatch,
		       params,
		       method, address, std::move(headers),
		       cancel_ptr);
}

[[gnu::pure]]
static bool
http_cache_may_serve(EventLoop &event_loop,
		     const HttpCacheRequestInfo &info,
		     const HttpCacheDocument &document) noexcept
{
	return info.only_if_cached ||
		(!info.no_cache && document.info.expires >= event_loop.SystemNow());
}

inline void
HttpCache::Found(const HttpCacheRequestInfo &info,
		 HttpCacheDocument &document,
		 const StringWithHash key,
		 struct pool &caller_pool,
		 const StopwatchPtr &parent_stopwatch,
		 const ResourceRequestParams &params,
		 HttpMethod method,
		 const ResourceAddress &address,
		 StringMap &&headers,
		 HttpResponseHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept
{
	++stats.hits;

	if (!info.no_cache && !CheckCacheRequest(caller_pool, info, document, handler))
		return;

	if (http_cache_may_serve(GetEventLoop(), info, document))
		Serve(caller_pool, document, key,
		      handler);
	else
		Revalidate(caller_pool, parent_stopwatch,
			   key, params,
			   info, document,
			   method, address, std::move(headers),
			   handler, cancel_ptr);
}

inline void
HttpCache::Use(struct pool &caller_pool,
	       const StopwatchPtr &parent_stopwatch,
	       const StringWithHash key,
	       const ResourceRequestParams &params,
	       HttpMethod method,
	       const ResourceAddress &address,
	       StringMap &&headers,
	       const HttpCacheRequestInfo &info,
	       HttpResponseHandler &handler,
	       CancellablePointer &cancel_ptr) noexcept
{
	auto *document = heap.Get(key, headers);

	if (document == nullptr)
		Miss(caller_pool, parent_stopwatch,
		     key, params, info,
		     method, address, std::move(headers),
		     handler, cancel_ptr);
	else
		Found(info, *document, key, caller_pool, parent_stopwatch,
		      params,
		      method, address, std::move(headers),
		      handler, cancel_ptr);
}

[[gnu::pure]]
static bool
IsHTTPS(const StringMap &headers) noexcept
{
	const char *https = headers.Get(x_cm4all_https_header);
	return https != nullptr && StringIsEqual(https, "on");
}

inline void
HttpCache::Start(struct pool &caller_pool,
		 const StopwatchPtr &parent_stopwatch,
		 const ResourceRequestParams &params,
		 HttpMethod method,
		 const ResourceAddress &address,
		 StringMap &&headers, UnusedIstreamPtr body,
		 HttpResponseHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept
{
	auto key = http_cache_key(caller_pool, address, params.address_id);
	if (/* this address type cannot be cached; skip the rest of this
	       library */
	    key.IsNull() ||
	    /* don't cache a huge request URI; probably it contains lots
	       and lots of unique parameters, and that's not worth the
	       cache space anyway */
	    key.value.size() > 8192) {
		resource_loader.SendRequest(caller_pool, parent_stopwatch,
					    params,
					    method, address,
					    std::move(headers),
					    std::move(body),
					    handler, cancel_ptr);
		return;
	}

	if (address.type == ResourceAddress::Type::LHTTP) {
		/* special case for Local HTTP: include the headers
		   "X-CM4all-HTTPS" and "X-CM4all-DocRoot" in the
		   cache key because these are usually used by our
		   modified LHTTP-Apache, but it doesn't set a "Vary"
		   header */

		const bool https = IsHTTPS(headers);
		const char *docroot = headers.Get(x_cm4all_docroot_header);

		if (https || docroot != nullptr) {
			char buffer[32];
			std::size_t docroot_hash = 0;
			std::string_view docroot_base32{};

			if (docroot != nullptr) {
				docroot_hash = djb_hash_string(docroot);
				docroot_base32 = {buffer, FormatIntBase32(buffer, docroot_hash)};
			}

			const AllocatorPtr alloc{caller_pool};
			key.value = alloc.Concat(https ? "https;"sv : std::string_view{},
						 docroot_base32,
						 docroot != nullptr ? "=dr;"sv : std::string_view{},
						 key.value);
			key.hash ^= docroot_hash + https;
		}
	}

	if (auto info = http_cache_request_evaluate(method, address, headers,
						    obey_no_cache && !params.ignore_no_cache,
						    body)) {
		assert(!body);

		Use(caller_pool, parent_stopwatch, key, params,
		    method, address, std::move(headers), *info,
		    handler, cancel_ptr);
	} else if (params.auto_flush_cache && IsModifyingMethod(method)) {
		LogConcat(4, "HttpCache", "auto_flush? ", key.value);
		++stats.skips;

		/* TODO merge IsModifyingMethod() and
		   http_cache_request_invalidate()? */
		Remove(key, headers);

		auto request =
			NewFromPool<AutoFlushHttpCacheRequest>(caller_pool,
							       params.cache_tag,
							       *this, handler);
		request->Start(resource_loader, caller_pool, parent_stopwatch,
			       params,
			       method, address,
			       std::move(headers), std::move(body),
			       cancel_ptr);
	} else {
		if (http_cache_request_invalidate(method))
			Remove(key, headers);

		LogConcat(4, "HttpCache", "ignore ", key.value);
		++stats.skips;

		resource_loader.SendRequest(caller_pool, parent_stopwatch,
					    params,
					    method, address,
					    std::move(headers),
					    std::move(body),
					    handler, cancel_ptr);
	}
}

void
http_cache_request(HttpCache &cache,
		   struct pool &pool,
		   const StopwatchPtr &parent_stopwatch,
		   const ResourceRequestParams &params,
		   HttpMethod method,
		   const ResourceAddress &address,
		   StringMap &&headers, UnusedIstreamPtr body,
		   HttpResponseHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept
{
	cache.Start(pool, parent_stopwatch, params,
		    method, address, std::move(headers), std::move(body),
		    handler, cancel_ptr);
}
