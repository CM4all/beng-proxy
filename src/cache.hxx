// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/CleanupTimer.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"

#include <chrono>
#include <memory>

#include <stddef.h>

class EventLoop;

/**
 * Use #SharedLease with the #SharedAnchor base class to prevent items
 * from getting removed and freed while you are still using them.
 */
class CacheItem : public SharedAnchor {
	friend class Cache;

	/**
	 * This item's siblings, sorted by #last_accessed.
	 */
	IntrusiveListHook<IntrusiveHookMode::NORMAL> sorted_siblings;

	IntrusiveHashSetHook<IntrusiveHookMode::NORMAL> set_hook;

	/**
	 * The key under which this item is stored in the hash table.
	 */
	const char *key;

	std::chrono::steady_clock::time_point expires;

	const size_t size;

	std::chrono::steady_clock::time_point last_accessed{};

	/**
	 * If true, then this item has been removed from the cache, but
	 * could not be destroyed yet, because it is locked.
	 */
	bool removed = false;

public:
	CacheItem(std::chrono::steady_clock::time_point _expires,
		  size_t _size) noexcept
		:expires(_expires), size(_size) {}

	CacheItem(std::chrono::steady_clock::time_point now,
		  std::chrono::system_clock::time_point system_now,
		  std::chrono::system_clock::time_point _expires,
		  size_t _size) noexcept;

	CacheItem(std::chrono::steady_clock::time_point now,
		  std::chrono::seconds max_age, size_t _size) noexcept;

	CacheItem(const CacheItem &) = delete;

	void Release() noexcept;

	const char *GetKey() const noexcept {
		return key;
	}

	void SetExpires(std::chrono::steady_clock::time_point _expires) noexcept {
		expires = _expires;
	}

	void SetExpires(std::chrono::steady_clock::time_point steady_now,
			std::chrono::system_clock::time_point system_now,
			std::chrono::system_clock::time_point _expires) noexcept;

	size_t GetSize() const noexcept {
		return size;
	}

	[[gnu::pure]]
	bool Validate(std::chrono::steady_clock::time_point now) const noexcept {
		return now < expires && Validate();
	}

	virtual bool Validate() const noexcept {
		return true;
	}

	virtual void Destroy() noexcept = 0;

	struct Hash {
		[[gnu::pure]]
		size_t operator()(const char *key) const noexcept;
	};

	struct Equal {
		[[gnu::pure]]
		bool operator()(const char *a, const char *b) const noexcept;
	};

	struct GetKeyFunction {
		[[gnu::pure]]
		const char *operator()(const CacheItem &item) const noexcept {
			return item.GetKey();
		}
	};

protected:
	/* virtual methods from SharedAnchor */
	virtual void OnAbandoned() noexcept;
	
};

class CacheHandler {
public:
	virtual void OnCacheItemAdded(const CacheItem &item) noexcept = 0;
	virtual void OnCacheItemRemoved(const CacheItem &item) noexcept = 0;
};

class Cache {
	const size_t max_size;
	size_t size = 0;

	CacheHandler *const handler;

	using ItemSet = IntrusiveHashSet<CacheItem, 65536,
					 IntrusiveHashSetOperators<CacheItem::Hash,
								   CacheItem::Equal,
								   CacheItem::GetKeyFunction>,
					 IntrusiveHashSetMemberHookTraits<&CacheItem::set_hook>>;

	ItemSet items;

	/**
	 * A linked list of all cache items, sorted by last_accessed,
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

	void EventAdd() noexcept;
	void EventDel() noexcept;

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
	bool Add(const char *key, CacheItem &item) noexcept;

	bool Put(const char *key, CacheItem &item) noexcept;

	/**
	 * Adds a new item to this cache, or replaces an existing item
	 * which matches with the specified matching function.
	 *
	 * @param key the cache item key
	 * @param item the new cache item
	 * @param match the match callback function
	 * @param ctx a context pointer for the callback
	 */
	bool PutMatch(const char *key, CacheItem &item,
		      bool (*match)(const CacheItem *, void *),
		      void *ctx) noexcept;

	void Remove(const char *key) noexcept;

	/**
	 * Removes all matching cache items.
	 *
	 * @return the number of items which were removed
	 */
	void RemoveMatch(const char *key,
			 bool (*match)(const CacheItem *, void *),
			 void *ctx) noexcept;

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

	void RefreshItem(CacheItem &item,
			 std::chrono::steady_clock::time_point now) noexcept;

	void DestroyOldestItem() noexcept;

	bool NeedRoom(size_t _size) noexcept;
};
