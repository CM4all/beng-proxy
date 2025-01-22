// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Item.hxx"
#include "event/CleanupTimer.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"

#include <chrono>
#include <memory>

#include <stddef.h>

class CacheItem;
class CacheHandler;

class Cache {
	const size_t max_size;
	size_t size = 0;

	CacheHandler *const handler;

	using ItemSet = IntrusiveHashSet<CacheItem, 65536,
					 IntrusiveHashSetOperators<CacheItem,
								   CacheItem::GetKeyFunction,
								   CacheItem::Hash,
								   CacheItem::Equal>,
					 IntrusiveHashSetMemberHookTraits<&CacheItem::set_hook>>;

	ItemSet items;

	/**
	 * A linked list of all cache items, sorted by last access,
	 * oldest first.
	 */
	IntrusiveList<CacheItem,
		      IntrusiveListMemberHookTraits<&CacheItem::sorted_siblings>> sorted_items;

	CleanupTimer cleanup_timer;

public:
	Cache(EventLoop &event_loop, size_t _max_size,
	      CacheHandler *_handler=nullptr) noexcept;

	~Cache() noexcept;

	auto &GetEventLoop() const noexcept {
		return cleanup_timer.GetEventLoop();
	}

	[[gnu::pure]]
	std::chrono::steady_clock::time_point SteadyNow() const noexcept;

	[[gnu::pure]]
	std::chrono::system_clock::time_point SystemNow() const noexcept;

	[[gnu::pure]]
	CacheItem *Get(const char *key) noexcept;

	/**
	 * Find the first CacheItem for a key which matches with the
	 * specified matching function.
	 *
	 * @param key the cache item key
	 * @param match the match callback function
	 * @param ctx a context pointer for the callback
	 */
	[[gnu::pure]]
	CacheItem *GetMatch(const char *key,
			    bool (*match)(const CacheItem *, void *),
			    void *ctx) noexcept;

	/**
	 * Add an item to this cache.  Item with the same key are preserved.
	 *
	 * @return false if the item could not be added to the cache due
	 * to size constraints
	 */
	bool Add(CacheItem &item) noexcept;

	bool Put(CacheItem &item) noexcept;

	/**
	 * Adds a new item to this cache, or replaces an existing item
	 * which matches with the specified matching function.
	 *
	 * @param key the cache item key
	 * @param item the new cache item
	 * @param match the match callback function
	 * @param ctx a context pointer for the callback
	 */
	bool PutMatch(CacheItem &item,
		      bool (*match)(const CacheItem *, void *),
		      void *ctx) noexcept;

	void Remove(const char *key) noexcept;

	/**
	 * Removes all matching cache items.
	 *
	 * @return the number of items which were removed
	 */
	void RemoveKeyIf(const char *key,
			 std::predicate<const CacheItem &> auto pred) noexcept {
		items.remove_and_dispose_key_if(key, pred, ItemRemover{*this});
	}

	void Remove(CacheItem &item) noexcept;

	/**
	 * Removes all matching cache items.
	 *
	 * @return the number of items which were removed
	 */
	unsigned RemoveAllMatch(bool (*match)(const CacheItem *, void *),
				void *ctx) noexcept;

	void Flush() noexcept;

private:
	/** clean up expired cache items every 60 seconds */
	bool ExpireCallback() noexcept;

	void ItemRemoved(CacheItem *item) noexcept;

	class ItemRemover {
		Cache &cache;

	public:
		explicit constexpr ItemRemover(Cache &_cache) noexcept
			:cache(_cache) {}

		void operator()(CacheItem *item) noexcept {
			cache.ItemRemoved(item);
		}
	};

	void RemoveItem(CacheItem &item) noexcept;

	void RefreshItem(CacheItem &item) noexcept;

	void DestroyOldestItem() noexcept;

	bool NeedRoom(size_t _size) noexcept;
};
