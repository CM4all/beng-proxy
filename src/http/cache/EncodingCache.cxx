// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "EncodingCache.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/SharedLeaseIstream.hxx"
#include "istream/TeeIstream.hxx"
#include "cache/Item.hxx"
#include "memory/istream_rubber.hxx"
#include "memory/sink_rubber.hxx"
#include "pool/pool.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/Loop.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/LeakDetector.hxx"

#include <string>

static constexpr off_t cacheable_size_limit = 512 * 1024;

/**
 * The default "expires" duration [s] if no expiration was given for
 * the input.
 */
static constexpr auto encoding_cache_default_expires = std::chrono::hours(7 * 24);

class EncodingCacheItemKey {
protected:
	const std::string key;

public:
	[[nodiscard]]
	explicit EncodingCacheItemKey(std::string_view _key) noexcept
		:key(_key) {}
};

struct EncodingCache::Item final : EncodingCacheItemKey, CacheItem, LeakDetector {
	const std::string key;


	const RubberAllocation allocation;

	Item(StringWithHash _key,
	     std::chrono::steady_clock::time_point now,
	     std::chrono::system_clock::time_point system_now,
	     std::size_t _size, RubberAllocation &&_allocation) noexcept
		:EncodingCacheItemKey(_key.value),
		 CacheItem(StringWithHash{EncodingCacheItemKey::key, _key.hash},
			   _size, now, system_now,
			   system_now + encoding_cache_default_expires),
		 allocation(std::move(_allocation)) {}

	/* virtual methods from class CacheItem */
	void Destroy() noexcept override {
		delete this;
	}

};

class EncodingCache::Store final
	: public AutoUnlinkIntrusiveListHook, RubberSinkHandler, LeakDetector
{
	static constexpr Event::Duration timeout = std::chrono::minutes(1);

	EncodingCache &cache;

	const StringWithHash key;

	/**
	 * This event is initialized by the response callback, and limits
	 * the duration for receiving the response body.
	 */
	CoarseTimerEvent timeout_event;

	/**
	 * To cancel the RubberSink.
	 */
	CancellablePointer rubber_cancel_ptr;

public:
	Store(EncodingCache &_cache, StringWithHash _key) noexcept
		:cache(_cache), key(_key),
		 timeout_event(cache.GetEventLoop(), BIND_THIS_METHOD(OnTimeout)) {}

	/**
	 * Release resources held by this request.
	 */
	void Destroy() noexcept {
		assert(!rubber_cancel_ptr);

		this->~Store();
	}

	void Start(struct pool &pool, UnusedIstreamPtr &&src) noexcept {
		timeout_event.Schedule(timeout);

		sink_rubber_new(pool, std::move(src),
				cache.rubber, cacheable_size_limit,
				*this,
				rubber_cancel_ptr);
	}

	/**
	 * Cancel storing the response body.
	 */
	void CancelStore() noexcept {
		assert(rubber_cancel_ptr);

		rubber_cancel_ptr.Cancel();
		Destroy();
	}

private:
	void OnTimeout() noexcept {
		/* reading the response has taken too long already; don't store
		   this resource */
		LogConcat(4, "EncodingCache", "timeout ", key.value);
		CancelStore();
	}

	/* virtual methods from class RubberSinkHandler */
	void RubberDone(RubberAllocation &&a, std::size_t size) noexcept override;
	void RubberOutOfMemory() noexcept override;
	void RubberTooLarge() noexcept override;
	void RubberError(std::exception_ptr ep) noexcept override;
};

void
EncodingCache::Store::RubberDone(RubberAllocation &&a, std::size_t size) noexcept
{
	rubber_cancel_ptr = nullptr;

	cache.Add(key, std::move(a), size);

	Destroy();
}

void
EncodingCache::Store::RubberOutOfMemory() noexcept
{
	rubber_cancel_ptr = nullptr;

	LogConcat(4, "EncodingCache", "nocache oom ", key.value);
	++cache.stats.skips;
	Destroy();
}

void
EncodingCache::Store::RubberTooLarge() noexcept
{
	rubber_cancel_ptr = nullptr;

	LogConcat(4, "EncodingCache", "nocache too large", key.value);
	++cache.stats.skips;
	Destroy();
}

void
EncodingCache::Store::RubberError(std::exception_ptr ep) noexcept
{
	rubber_cancel_ptr = nullptr;

	LogConcat(4, "EncodingCache", "body_error ", key.value, ": ", ep);
	++cache.stats.skips;
	Destroy();
}

UnusedIstreamPtr
EncodingCache::Get(struct pool &pool, StringWithHash key) noexcept
{
	auto *item = (EncodingCache::Item *)cache.Get(key);
	if (item == nullptr) {
		LogConcat(6, "EncodingCache", "miss ", key.value);
		++stats.misses;
		return {};
	}

	LogConcat(5, "EncodingCache", "hit ", key.value);
	++stats.hits;

	return NewSharedLeaseIstream(pool,
				     istream_rubber_new(pool, rubber, item->allocation.GetId(),
							0, item->GetSize(), false),
				     *item);
}

UnusedIstreamPtr
EncodingCache::Put(struct pool &pool,
		   StringWithHash key,
		   UnusedIstreamPtr src) noexcept
{
	if (!src)
		return src;

	if (const auto available = src.GetAvailable(true);
	    available > cacheable_size_limit) {
		/* too large for the cache */
		LogConcat(4, "EncodingCache", "nocache too large", key.value);
		++stats.skips;
		return src;
	}

	LogConcat(4, "EncodingCache", "put ", key.value);

	/* tee the body: one goes to our client, and one goes into the
	   cache */
	src = NewTeeIstream(pool, std::move(src),
			    GetEventLoop(),
			    false, false);

	auto store = NewFromPool<Store>(pool, *this, key);
	stores.push_back(*store);

	store->Start(pool, AddTeeIstream(src, true));

	return src;
}

void
EncodingCache::Add(StringWithHash key,
		   RubberAllocation &&a, std::size_t size) noexcept
{
	LogConcat(4, "EncodingCache", "add ", key.value);
	++stats.stores;

	auto item = new Item(key,
			     cache.SteadyNow(),
			     cache.SystemNow(),
			     size,
			     std::move(a));

	cache.Put(*item);
}

EncodingCache::EncodingCache(EventLoop &_event_loop, size_t max_size)
	:rubber(max_size, "encoding_cache"),
	 /* leave 12.5% of the rubber allocator empty, to increase the
	    chances that a hole can be found for a new allocation, to
	    reduce the pressure that rubber_compress() creates */
	 cache(_event_loop, max_size * 7 / 8),
	 compress_timer(_event_loop, BIND_THIS_METHOD(OnCompressTimer))
{
	compress_timer.Schedule(compress_interval);
}

EncodingCache::~EncodingCache() noexcept
{
	stores.clear_and_dispose([](auto *r){ r->CancelStore(); });
}
