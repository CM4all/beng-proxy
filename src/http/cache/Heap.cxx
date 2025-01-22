// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Heap.hxx"
#include "Item.hxx"
#include "memory/AllocatorStats.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/SharedLeaseIstream.hxx"
#include "pool/pool.hxx"
#include "AllocatorPtr.hxx"

static bool
http_cache_item_match(const CacheItem *_item, void *ctx) noexcept
{
	const auto &item = *(const HttpCacheItem *)_item;
	const auto &headers = *(const StringMap *)ctx;

	return item.VaryFits(headers);
}

HttpCacheDocument *
HttpCacheHeap::Get(StringWithHash key, StringMap &request_headers) noexcept
{
	return (HttpCacheItem *)cache.GetMatch(key,
					       http_cache_item_match,
					       &request_headers);
}

void
HttpCacheHeap::Put(StringWithHash key, const char *tag,
		   const HttpCacheResponseInfo &info,
		   const StringMap &request_headers,
		   HttpStatus status,
		   const StringMap &response_headers,
		   RubberAllocation &&a, size_t size) noexcept
{
	auto new_pool = pool_new_slice(pool, "http_cache_item", slice_pool);
	const AllocatorPtr alloc{new_pool};
	key = alloc.Dup(key);

	auto item = NewFromPool<HttpCacheItem>(std::move(new_pool), key,
					       cache.SteadyNow(),
					       cache.SystemNow(),
					       tag,
					       info, request_headers,
					       status, response_headers,
					       size,
					       std::move(a));

	if (tag != nullptr)
		per_tag.insert(*item);

	cache.PutMatch(*item,
		       http_cache_item_match,
		       const_cast<void *>((const void *)&request_headers));
}

void
HttpCacheHeap::Remove(HttpCacheDocument &document) noexcept
{
	auto &item = (HttpCacheItem &)document;

	cache.Remove(item);
}

void
HttpCacheHeap::Remove(StringWithHash key, const StringMap &headers) noexcept
{
	cache.RemoveKeyIf(key, [&headers](const CacheItem &_item){
		const auto &item = static_cast<const HttpCacheItem &>(_item);
		return item.VaryFits(headers);
	});
}

void
HttpCacheHeap::ForkCow(bool inherit) noexcept
{
	slice_pool.ForkCow(inherit);
	rubber.ForkCow(inherit);
}

void
HttpCacheHeap::Compress() noexcept
{
	slice_pool.Compress();
	rubber.Compress();
}

void
HttpCacheHeap::Flush() noexcept
{
	cache.Flush();
	slice_pool.Compress();
	rubber.Compress();
}

void
HttpCacheHeap::FlushTag(std::string_view tag) noexcept
{
	per_tag.remove_and_dispose_key(tag, [this](auto *item){
		cache.Remove(*item);
	});
}

SharedLease
HttpCacheHeap::Lock(HttpCacheDocument &document) noexcept
{
	auto &item = (HttpCacheItem &)document;
	return item;
}

UnusedIstreamPtr
HttpCacheHeap::OpenStream(struct pool &_pool,
			  HttpCacheDocument &document) noexcept
{
	auto &item = (HttpCacheItem &)document;

	if (!item.HasBody())
		/* don't lock the item */
		return {};

	return NewSharedLeaseIstream(_pool, item.OpenStream(_pool), item);
}

/*
 * cache_class
 *
 */

HttpCacheHeap::HttpCacheHeap(struct pool &_pool, EventLoop &event_loop,
			     size_t max_size) noexcept
	:pool(_pool),
	 slice_pool(1024, 65536, "http_cache_meta"),
	 rubber(max_size, "http_cache_data"),
	 /* leave 12.5% of the rubber allocator empty, to increase the
	    chances that a hole can be found for a new allocation, to
	    reduce the pressure that rubber_compress() creates */
	 cache(event_loop, max_size * 7 / 8)
{
}

HttpCacheHeap::~HttpCacheHeap() noexcept = default;

AllocatorStats
HttpCacheHeap::GetStats() const noexcept
{
	return slice_pool.GetStats() + rubber.GetStats();
}
