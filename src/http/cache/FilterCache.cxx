// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FilterCache.hxx"
#include "strmap.hxx"
#include "cache/Cache.hxx"
#include "cache/Item.hxx"
#include "http/CommonHeaders.hxx"
#include "http/ResponseHandler.hxx"
#include "http/rl/ResourceLoader.hxx"
#include "AllocatorPtr.hxx"
#include "ResourceAddress.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_null.hxx"
#include "istream/SharedLeaseIstream.hxx"
#include "istream/TeeIstream.hxx"
#include "istream/RefIstream.hxx"
#include "memory/istream_rubber.hxx"
#include "memory/Rubber.hxx"
#include "memory/sink_rubber.hxx"
#include "memory/SlicePool.hxx"
#include "stats/CacheStats.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "pool/Holder.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/FarTimerEvent.hxx"
#include "event/Loop.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "io/Logger.hxx"
#include "http/List.hxx"
#include "http/Date.hxx"
#include "http/Method.hxx"
#include "util/Cancellable.hxx"
#include "util/djb_hash.hxx"
#include "util/Exception.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/LeakDetector.hxx"
#include "util/SpanCast.hxx"

#include <stdio.h>
#include <unistd.h>

static constexpr off_t cacheable_size_limit = 512 * 1024;

/**
 * The timeout for the underlying HTTP request.  After this timeout
 * expires, the filter cache gives up and doesn't store the response.
 */
static constexpr Event::Duration fcache_request_timeout = std::chrono::minutes(1);

static constexpr Event::Duration fcache_compress_interval = std::chrono::minutes(10);

/**
 * The default "expires" duration [s] if no expiration was given for
 * the input.
 */
static constexpr auto fcache_default_expires = std::chrono::hours(7 * 24);

struct FilterCacheInfo {
	/** when will the cached resource expire? (beng-proxy time) */
	std::chrono::system_clock::time_point expires =
		std::chrono::system_clock::from_time_t(-1);

	const char *tag;

	/** the final resource id */
	StringWithHash key;

	FilterCacheInfo(const char *_tag, StringWithHash _key) noexcept
		:tag(_tag), key(_key) {}

	FilterCacheInfo(AllocatorPtr alloc, const FilterCacheInfo &src) noexcept
		:expires(src.expires),
		 tag(alloc.CheckDup(src.tag)),
		 key(alloc.Dup(src.key)) {}

	FilterCacheInfo(FilterCacheInfo &&src) = default;

	FilterCacheInfo &operator=(const FilterCacheInfo &) = delete;
};

struct FilterCacheItem final : PoolHolder, CacheItem, LeakDetector {
	const char *const tag;

	/**
	 * For #FilterCache::per_tag.
	 */
	IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK> per_tag_hook;

	const HttpStatus status;
	StringMap headers;

	const size_t size;

	const RubberAllocation body;

	struct TagHash {
		[[gnu::pure]]
		std::size_t operator()(const char *_tag) const noexcept {
			return djb_hash_string(_tag);
		}

		[[gnu::pure]]
		std::size_t operator()(std::string_view _tag) const noexcept {
			return djb_hash(AsBytes(_tag));
		}
	};

	struct GetTag {
		[[gnu::pure]]
		std::string_view operator()(const FilterCacheItem &item) const noexcept {
			return item.tag;
		}
	};

	FilterCacheItem(PoolPtr &&_pool,
			StringWithHash _key,
			std::chrono::steady_clock::time_point now,
			std::chrono::system_clock::time_point system_now,
			const char *_tag,
			HttpStatus _status, const StringMap &_headers,
			size_t _size, RubberAllocation &&_body,
			std::chrono::system_clock::time_point _expires) noexcept
		:PoolHolder(std::move(_pool)),
		 CacheItem(_key, pool_netto_size(pool) + _size, now, system_now, _expires),
		 tag(_tag != nullptr ? p_strdup(GetPool(), _tag) : nullptr),
		 status(_status), headers(pool, _headers),
		 size(_size), body(std::move(_body)) {
	}

	using PoolHolder::GetPool;

	/* virtual methods from class CacheItem */
	void Destroy() noexcept override {
		pool_trash(pool);
		this->~FilterCacheItem();
	}

};

class FilterCacheRequest final
	: PoolHolder, HttpResponseHandler, RubberSinkHandler,
	  Cancellable, LeakDetector {
	const PoolPtr caller_pool;
	FilterCache &cache;
	HttpResponseHandler &handler;

	FilterCacheInfo info;

	struct {
		HttpStatus status;
		StringMap *headers;

		/**
		 * A handle to abort the sink_rubber that copies response body
		 * data into a new rubber allocation.
		 */
		CancellablePointer cancel_ptr;
	} response;

	/**
	 * This event is initialized by the response callback, and limits
	 * the duration for receiving the response body.
	 */
	CoarseTimerEvent timeout_event;

	CancellablePointer cancel_ptr;

	AutoUnlinkIntrusiveListHook siblings;

public:
	using List =
		IntrusiveList<FilterCacheRequest,
			      IntrusiveListMemberHookTraits<&FilterCacheRequest::siblings>>;

	FilterCacheRequest(PoolPtr &&_pool, struct pool &_caller_pool,
			   FilterCache &_cache,
			   HttpResponseHandler &_handler,
			   const FilterCacheInfo &_info) noexcept;

	void Start(ResourceLoader &resource_loader,
		   const StopwatchPtr &parent_stopwatch,
		   const char *cache_tag,
		   const ResourceAddress &address,
		   HttpStatus status, StringMap &&headers,
		   UnusedIstreamPtr body, StringWithHash body_etag,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		resource_loader.SendRequest(pool, parent_stopwatch,
					    {
						    .status = status,
						    .body_etag = body_etag,
						    .cache_tag = cache_tag,
					    },
					    HttpMethod::POST, address,
					    std::move(headers),
					    std::move(body),
					    *this,
					    cancel_ptr);
	}

	/**
	 * Release resources held by this request.
	 */
	void Destroy() noexcept;

	/**
	 * Cancel storing the response body.
	 */
	void CancelStore() noexcept;

private:
	void OnTimeout() noexcept;

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

class FilterCache final : LeakDetector {
	friend class FilterCacheRequest;

	PoolPtr pool;
	SlicePool slice_pool;
	Rubber rubber;
	Cache cache;

	/**
	 * Lookup table to speed up FlushTag().
	 */
	IntrusiveHashSet<FilterCacheItem, 65536,
			 IntrusiveHashSetOperators<FilterCacheItem,
						   FilterCacheItem::GetTag,
						   FilterCacheItem::TagHash,
						   std::equal_to<std::string_view>>,
			 IntrusiveHashSetMemberHookTraits<&FilterCacheItem::per_tag_hook>> per_tag;

	FarTimerEvent compress_timer;

	ResourceLoader &resource_loader;

	/**
	 * A list of requests that are currently copying the response body
	 * to a #Rubber allocation.  We keep track of them so we can
	 * cancel them on shutdown.
	 */
	FilterCacheRequest::List requests;

	mutable CacheStats stats{};

public:
	FilterCache(struct pool &_pool, size_t max_size,
		    EventLoop &_event_loop, ResourceLoader &_resource_loader);

	~FilterCache() noexcept;

	auto &GetEventLoop() const noexcept {
		return compress_timer.GetEventLoop();
	}

	void ForkCow(bool inherit) noexcept {
		rubber.ForkCow(inherit);
		slice_pool.ForkCow(inherit);
	}

	CacheStats GetStats() const noexcept {
		stats.allocator = slice_pool.GetStats() + rubber.GetStats();
		return stats;
	}

	void Flush() noexcept {
		cache.Flush();
		Compress();
	}

	void FlushTag(std::string_view tag) noexcept;

	void Get(struct pool &caller_pool,
		 const StopwatchPtr &parent_stopwatch,
		 const char *cache_tag,
		 const ResourceAddress &address,
		 StringWithHash source_id,
		 HttpStatus status, StringMap &&headers,
		 UnusedIstreamPtr body,
		 HttpResponseHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

	void Put(const FilterCacheInfo &info,
		 HttpStatus status, const StringMap &headers,
		 RubberAllocation &&a, size_t size) noexcept;

private:
	void Miss(struct pool &caller_pool,
		  const StopwatchPtr &parent_stopwatch,
		  FilterCacheInfo &&info,
		  const ResourceAddress &address,
		  HttpStatus status, StringMap &&headers,
		  UnusedIstreamPtr body, StringWithHash body_etag,
		  HttpResponseHandler &_handler,
		  CancellablePointer &cancel_ptr) noexcept;

	void Serve(FilterCacheItem &item,
		   struct pool &caller_pool,
		   HttpResponseHandler &handler) noexcept;

	void Hit(FilterCacheItem &item,
		 struct pool &caller_pool,
		 HttpResponseHandler &handler) noexcept;

	void Compress() noexcept {
		rubber.Compress();
		slice_pool.Compress();
	}

	void OnCompressTimer() noexcept {
		Compress();
		compress_timer.Schedule(fcache_compress_interval);
	}
};

FilterCacheRequest::FilterCacheRequest(PoolPtr &&_pool,
				       struct pool &_caller_pool,
				       FilterCache &_cache,
				       HttpResponseHandler &_handler,
				       const FilterCacheInfo &_info) noexcept
	:PoolHolder(std::move(_pool)), caller_pool(_caller_pool),
	 cache(_cache),
	 handler(_handler),
	 info(GetPool(), _info),
	 timeout_event(cache.GetEventLoop(), BIND_THIS_METHOD(OnTimeout))
{
}

void
FilterCacheRequest::Destroy() noexcept
{
	assert(!response.cancel_ptr);

	this->~FilterCacheRequest();
}

void
FilterCacheRequest::CancelStore() noexcept
{
	assert(response.cancel_ptr);

	response.cancel_ptr.Cancel();
	Destroy();
}

/* check whether the request could produce a cacheable response */
static FilterCacheInfo *
filter_cache_request_evaluate(AllocatorPtr alloc,
			      const char *tag,
			      const ResourceAddress &address,
			      StringWithHash source_id,
			      const StringMap &headers) noexcept
{
	if (source_id.IsNull())
		return nullptr;

	const char *user = headers.Get(x_cm4all_beng_user_header);
	if (user == nullptr)
		user = "";

	const StringWithHash user_id{user};
	const StringWithHash address_id = address.GetId(alloc);

	const StringWithHash key{
		alloc.ConcatView(source_id.value, '|', user_id.value, '|', address_id.value),
		source_id.hash ^ user_id.hash ^ address_id.hash,
	};

	return alloc.New<FilterCacheInfo>(tag, key);
}

void
FilterCache::Put(const FilterCacheInfo &info,
		 HttpStatus status, const StringMap &headers,
		 RubberAllocation &&a, size_t size) noexcept
{
	LogConcat(4, "FilterCache", "put ", info.key.value);

	std::chrono::system_clock::time_point expires;
	if (info.expires == std::chrono::system_clock::from_time_t(-1))
		expires = GetEventLoop().SystemNow() + fcache_default_expires;
	else
		expires = info.expires;

	auto new_pool = pool_new_slice(*pool, "FilterCacheItem", slice_pool);
	const StringWithHash key = AllocatorPtr{new_pool}.Dup(info.key);

	auto item = NewFromPool<FilterCacheItem>(std::move(new_pool),
						 key,
						 cache.SteadyNow(),
						 cache.SystemNow(),
						 info.tag,
						 status, headers, size,
						 std::move(a),
						 expires);

	if (info.tag != nullptr)
		per_tag.insert(*item);

	cache.Put(*item);
}

static std::chrono::system_clock::time_point
parse_translate_time(const char *p,
		     std::chrono::system_clock::duration offset) noexcept
{
	if (p == nullptr)
		return std::chrono::system_clock::from_time_t(-1);

	auto t = http_date_parse(p);
	if (t != std::chrono::system_clock::from_time_t(-1))
		t += offset;

	return t;
}

static constexpr bool
CanCacheStatus(HttpStatus status) noexcept
{
	return status == HttpStatus::OK || status == HttpStatus::NO_CONTENT;
}

/** check whether the HTTP response should be put into the cache */
static bool
filter_cache_response_evaluate(EventLoop &event_loop, FilterCacheInfo &info,
			       HttpStatus status, const StringMap &headers,
			       off_t body_available) noexcept
{
	const char *p;

	if (!CanCacheStatus(status))
		return false;

	if (body_available != (off_t)-1 && body_available > cacheable_size_limit)
		/* too large for the cache */
		return false;

	p = headers.Get(cache_control_header);
	if (p != nullptr && http_list_contains(p, "no-store"))
		return false;

	const auto now = event_loop.SystemNow();
	std::chrono::system_clock::duration offset = std::chrono::system_clock::duration::zero();

	p = headers.Get(date_header);
	if (p != nullptr) {
		auto date = http_date_parse(p);
		if (date != std::chrono::system_clock::from_time_t(-1))
			offset = now - date;
	}

	if (info.expires == std::chrono::system_clock::from_time_t(-1)) {
		info.expires = parse_translate_time(headers.Get(expires_header), offset);
		if (info.expires != std::chrono::system_clock::from_time_t(-1) &&
		    info.expires < now)
			LogConcat(2, "FilterCache", "invalid 'expires' header");
	}

	/*
	  info.out_etag = headers.Get(etag_header);
	*/

	return true;
}

inline void
FilterCacheRequest::OnTimeout() noexcept
{
	/* reading the response has taken too long already; don't store
	   this resource */
	LogConcat(4, "FilterCache", "timeout ", info.key.value);
	CancelStore();
}

/*
 * RubberSinkHandler
 *
 */

void
FilterCacheRequest::RubberDone(RubberAllocation &&a, size_t size) noexcept
{
	response.cancel_ptr = nullptr;

	/* the request was successful, and all of the body data has been
	   saved: add it to the cache */
	cache.Put(info, response.status, *response.headers, std::move(a), size);

	Destroy();
}

void
FilterCacheRequest::RubberOutOfMemory() noexcept
{
	response.cancel_ptr = nullptr;

	LogConcat(4, "FilterCache", "nocache oom ", info.key.value);
	Destroy();
}

void
FilterCacheRequest::RubberTooLarge() noexcept
{
	response.cancel_ptr = nullptr;

	LogConcat(4, "FilterCache", "nocache too large ", info.key.value);
	Destroy();
}

void
FilterCacheRequest::RubberError(std::exception_ptr ep) noexcept
{
	response.cancel_ptr = nullptr;

	LogConcat(4, "FilterCache", "body_abort ", info.key.value, ": ", ep);
	Destroy();
}

void
FilterCacheRequest::Cancel() noexcept
{
	cancel_ptr.Cancel();
	Destroy();
}

/*
 * http response handler
 *
 */

void
FilterCacheRequest::OnHttpResponse(HttpStatus status, StringMap &&headers,
				   UnusedIstreamPtr body) noexcept
{
	/* make sure the caller pool gets unreferenced upon returning */
	const auto _caller_pool = std::move(caller_pool);

	off_t available = body ? body.GetAvailable(true) : 0;

	if (!filter_cache_response_evaluate(cache.GetEventLoop(), info,
					    status, headers, available)) {
		/* don't cache response */
		LogConcat(4, "FilterCache", "nocache ", info.key.value);
		++cache.stats.skips;

		if (body)
			body = NewRefIstream(pool, std::move(body));
		else
			/* workaround: if there is no response body,
			   nobody will hold a pool reference, and the
			   headers will be freed after
			   InvokeResponse() returns; in that case, we
			   need to copy all headers into the caller's
			   pool to avoid use-after-free bugs */
			headers = {caller_pool, headers};

		handler.InvokeResponse(status, std::move(headers), std::move(body));
		Destroy();
		return;
	}

	++cache.stats.stores;

	/* copy the HttpResponseHandler reference to the stack, because
	   the sink_rubber_new() call may destroy this object */
	auto &_handler = handler;

	/* pool reference necessary because our destructor will free
	   the pool, which will free all "headers" strings, which we
	   are going to pass to our handler - destroy the pool only
	   after the handler has returned */
	const ScopePoolRef ref(pool);

	if (!body) {
		response.cancel_ptr = nullptr;

		cache.Put(info, status, headers, {}, 0);

		/* workaround: if there is no response body, nobody
		   will hold a pool reference, and the headers will be
		   freed after InvokeResponse() returns; in that case,
		   we need to copy all headers into the caller's pool
		   to avoid use-after-free bugs */
		headers = {caller_pool, headers};

		Destroy();
		_handler.InvokeResponse(status, std::move(headers), std::move(body));
	} else {
		/* tee the body: one goes to our client, and one goes into the
		   cache */
		auto tee1 = NewTeeIstream(pool, std::move(body),
					  cache.GetEventLoop(),
					  false,
					  /* just in case our handler closes
					     the body without looking at it:
					     defer an Istream::Read() call for
					     the Rubber sink */
					  true);

		auto tee2 = AddTeeIstream(tee1,
					  /* the second one must be weak
					     because closing the first one may
					     imply invalidating our input
					     (because its pool is going to be
					     trashed), triggering the pool
					     leak detector */
					  true);

		response.status = status;
		response.headers = strmap_dup(pool, &headers);

		cache.requests.push_front(*this);

		timeout_event.Schedule(fcache_request_timeout);

		sink_rubber_new(pool, std::move(tee2),
				cache.rubber, cacheable_size_limit,
				*this,
				response.cancel_ptr);

		body = std::move(tee1);

		_handler.InvokeResponse(status, std::move(headers), std::move(body));
	}
}

void
FilterCacheRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	ep = NestException(ep, FmtRuntimeError("fcache {}", info.key.value));

	handler.InvokeError(ep);
	Destroy();
}

/*
 * constructor and public methods
 *
 */

FilterCache::FilterCache(struct pool &_pool, size_t max_size,
			 EventLoop &_event_loop,
			 ResourceLoader &_resource_loader)
	:pool(pool_new_dummy(&_pool, "filter_cache")),
	 slice_pool(1024, 65536, "filter_cache_meta"),
	 rubber(max_size, "filter_cache_data"),
	 /* leave 12.5% of the rubber allocator empty, to increase the
	    chances that a hole can be found for a new allocation, to
	    reduce the pressure that rubber_compress() creates */
	 cache(_event_loop, max_size * 7 / 8),
	 compress_timer(_event_loop, BIND_THIS_METHOD(OnCompressTimer)),
	 resource_loader(_resource_loader) {
	compress_timer.Schedule(fcache_compress_interval);
}

FilterCache *
filter_cache_new(struct pool *pool, size_t max_size,
		 EventLoop &event_loop,
		 ResourceLoader &resource_loader)
{
	assert(max_size > 0);

	return new FilterCache(*pool, max_size,
			       event_loop, resource_loader);
}

inline FilterCache::~FilterCache() noexcept
{
	requests.clear_and_dispose([](FilterCacheRequest *r){ r->CancelStore(); });
}

void
filter_cache_close(FilterCache *cache) noexcept
{
	delete cache;
}

void
filter_cache_fork_cow(FilterCache &cache, bool inherit) noexcept
{
	cache.ForkCow(inherit);
}

CacheStats
filter_cache_get_stats(const FilterCache &cache) noexcept
{
	return cache.GetStats();
}

void
filter_cache_flush(FilterCache &cache) noexcept
{
	cache.Flush();
}

void
FilterCache::FlushTag(std::string_view tag) noexcept
{
	per_tag.remove_and_dispose_key(tag, [this](auto *item){
		cache.Remove(*item);
	});
}

void
filter_cache_flush_tag(FilterCache &cache, std::string_view tag) noexcept
{
	cache.FlushTag(tag);
}

void
FilterCache::Miss(struct pool &caller_pool,
		  const StopwatchPtr &parent_stopwatch,
		  FilterCacheInfo &&info,
		  const ResourceAddress &address,
		  HttpStatus status, StringMap &&headers,
		  UnusedIstreamPtr body, StringWithHash body_etag,
		  HttpResponseHandler &_handler,
		  CancellablePointer &cancel_ptr) noexcept
{
	/* the cache request may live longer than the caller pool, so
	   allocate a new pool for it from cache->pool */
	auto request_pool = pool_new_linear(pool, "filter_cache_request", 8192);

	auto request = NewFromPool<FilterCacheRequest>(std::move(request_pool),
						       caller_pool,
						       *this, _handler, info);

	LogConcat(4, "FilterCache", "miss ", info.key.value);
	++stats.misses;

	request->Start(resource_loader, parent_stopwatch, info.tag,
		       address,
		       status, std::move(headers),
		       std::move(body), body_etag,
		       cancel_ptr);
}

void
FilterCache::Serve(FilterCacheItem &item,
		   struct pool &caller_pool,
		   HttpResponseHandler &handler) noexcept
{
	LogConcat(4, "FilterCache", "serve ", item.GetKey().value);
	++stats.hits;

	/* XXX hold reference on item */

	assert(!item.body || ((CacheItem &)item).GetSize() >= item.size);

	auto response_body = item.body
		? istream_rubber_new(caller_pool, rubber, item.body.GetId(),
				     0, item.size, false)
		: istream_null_new(caller_pool);

	response_body = NewSharedLeaseIstream(caller_pool, std::move(response_body), item);

	handler.InvokeResponse(item.status,
			       StringMap(ShallowCopy(), caller_pool, item.headers),
			       std::move(response_body));
}

void
FilterCache::Hit(FilterCacheItem &item,
		 struct pool &caller_pool,
		 HttpResponseHandler &handler) noexcept
{
	Serve(item, caller_pool, handler);
}

void
FilterCache::Get(struct pool &caller_pool,
		 const StopwatchPtr &parent_stopwatch,
		 const char *cache_tag,
		 const ResourceAddress &address,
		 StringWithHash source_id,
		 HttpStatus status, StringMap &&headers,
		 UnusedIstreamPtr body,
		 HttpResponseHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept
{
	auto *info = filter_cache_request_evaluate(caller_pool, cache_tag, address,
						   source_id, headers);
	if (info != nullptr) {
		FilterCacheItem *item
			= (FilterCacheItem *)cache.Get(info->key);

		if (item == nullptr)
			Miss(caller_pool, parent_stopwatch,
			     std::move(*info),
			     address, status, std::move(headers),
			     std::move(body), source_id,
			     handler, cancel_ptr);
		else {
			body.Clear();
			Hit(*item, caller_pool, handler);
		}
	} else {
		++stats.skips;

		resource_loader.SendRequest(caller_pool, parent_stopwatch,
					    {
						    .status = status,
						    .body_etag = source_id,
						    .cache_tag = cache_tag,
					    },
					    HttpMethod::POST, address,
					    std::move(headers),
					    std::move(body),
					    handler, cancel_ptr);
	}
}

void
filter_cache_request(FilterCache &cache,
		     struct pool &pool,
		     const StopwatchPtr &parent_stopwatch,
		     const char *cache_tag,
		     const ResourceAddress &address,
		     StringWithHash source_id,
		     HttpStatus status, StringMap &&headers,
		     UnusedIstreamPtr body,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept
{
	cache.Get(pool, parent_stopwatch,
		  cache_tag, address, source_id,
		  status, std::move(headers),
		  std::move(body), handler, cancel_ptr);
}
