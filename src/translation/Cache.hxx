// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Service.hxx"
#include "cache/Cache.hxx"
#include "cache/Handler.hxx"
#include "pool/Ptr.hxx"
#include "stats/CacheStats.hxx"
#include "memory/SlicePool.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/TransparentHash.hxx"

#include <cstdint>
#include <memory>
#include <span>

enum class TranslationCommand : uint16_t;
class EventLoop;
struct CacheStats;
struct TranslateCacheItem;
struct TranslateCacheItemTag;
struct TranslateRequest;
struct TranslateResponse;

/**
 * Cache for translation server responses.
 */
class TranslationCache final : public TranslationService, CacheHandler {
	friend struct TranslateCacheRequest;

	const PoolPtr pool;
	SlicePool slice_pool;

	static constexpr std::size_t N_BUCKETS = 128 * 1024;

	struct GetHost {
		[[gnu::pure]]
		std::string_view operator()(const TranslateCacheItem &item) const noexcept;
	};

	struct PerHostSetHookTraits;

	/**
	 * This hash table maps each host name to the
	 * #TranslateCacheItem instances with that host.  This is used
	 * to optimize the common INVALIDATE=HOST response, to avoid
	 * traversing the whole cache.
	 */
	using PerHostSet =
		IntrusiveHashSet<TranslateCacheItem, N_BUCKETS,
				 IntrusiveHashSetOperators<TranslateCacheItem,
							   GetHost,
							   TransparentHash,
							   std::equal_to<std::string_view>>,
				 PerHostSetHookTraits>;
	PerHostSet per_host;

	struct GetSite {
		[[gnu::pure]]
		std::string_view operator()(const TranslateCacheItem &item) const noexcept;
	};

	struct PerSiteSetHookTraits;

	/**
	 * This hash table maps each site name to the
	 * #TranslateCacheItem instances with that site.  This is used
	 * to optimize the common INVALIDATE=SITE response, to avoid
	 * traversing the whole cache.
	 */
	using PerSiteSet =
		IntrusiveHashSet<TranslateCacheItem, N_BUCKETS,
				 IntrusiveHashSetOperators<TranslateCacheItem,
							   GetSite,
							   TransparentHash,
							   std::equal_to<std::string_view>>,
				 PerSiteSetHookTraits>;
	PerSiteSet per_site;

	struct GetTag {
		constexpr std::string_view operator()(const TranslateCacheItemTag &tag) const noexcept;
	};

	/**
	 * This hash table maps each tag string to the
	 * #TranslateCacheItem::Tag instances with that CACHE_TAG.
	 * This is used to optimize the common INVALIDATE=CACHE_TAG
	 * response, to avoid traversing the whole cache.
	 */
	using PerTagSet =
		IntrusiveHashSet<TranslateCacheItemTag, 256,
				 IntrusiveHashSetOperators<TranslateCacheItemTag,
							   GetTag,
							   TransparentHash,
							   std::equal_to<std::string_view>>>;
	PerTagSet per_tag;

	Cache cache;

	CacheStats stats{};

	TranslationService &next;

	/**
	 * This flag may be set to false when initializing the translation
	 * cache.  All responses will be regarded "non cacheable".  It
	 * will be set to true as soon as the first response is received.
	 */
	bool active;

public:
	/**
	 * @param handshake_cacheable if false, then all requests are
	 * deemed uncacheable until the first response is received
	 */
	TranslationCache(struct pool &pool, EventLoop &event_loop,
			 TranslationService &next,
			 unsigned max_size, bool handshake_cacheable=true);

	~TranslationCache() noexcept override;

	void ForkCow(bool inherit) noexcept;

	void Populate() noexcept;

	CacheStats GetStats() const noexcept {
		return stats;
	}

	/**
	 * Flush all items from the cache.
	 */
	void Flush() noexcept;

	/**
	 * Flush selected items from the cache.
	 *
	 * @param request a request with parameters to compare with
	 * @param vary a list of #beng_translation_command codes which define
	 * the cache item filter
	 */
	void Invalidate(const TranslateRequest &request,
			std::span<const TranslationCommand> vary,
			const char *site, const char *tag) noexcept;

	/* virtual methods from class TranslationService */
	void SendRequest(AllocatorPtr alloc,
			 const TranslateRequest &request,
			 const StopwatchPtr &parent_stopwatch,
			 TranslateHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;

private:
	[[gnu::pure]]
	TranslateCacheItem *Get(const TranslateRequest &request,
				StringWithHash key, bool find_base) noexcept;

	[[gnu::pure]]
	TranslateCacheItem *Lookup(const TranslateRequest &request,
				   StringWithHash key) noexcept;

	void Miss(AllocatorPtr alloc,
		  const TranslateRequest &request, StringWithHash key,
		  bool cacheable,
		  const StopwatchPtr &parent_stopwatch,
		  TranslateHandler &handler,
		  CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Throws std::runtime_error on error.
	 */
	const TranslateCacheItem *Store(const StringWithHash &key,
					const TranslateRequest &request,
					bool find_base,
					const TranslateResponse &response);

	std::size_t InvalidateHost(const TranslateRequest &request,
				   std::span<const TranslationCommand> vary,
				   const char *tag) noexcept;

	std::size_t InvalidateSite(const TranslateRequest &request,
				   std::span<const TranslationCommand> vary,
				   std::string_view site, const char *tag) noexcept;

	std::size_t InvalidateTag(const TranslateRequest &request,
				  std::span<const TranslationCommand> vary,
				  std::string_view tag) noexcept;

	/* virtual methods from CacheHandler */
	void OnCacheItemAdded(const CacheItem &_item) noexcept override;
	void OnCacheItemRemoved(const CacheItem &_item) noexcept override;
};
