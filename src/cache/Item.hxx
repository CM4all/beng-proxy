// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"
#include "util/StringWithHash.hxx"

#include <chrono>
#include <cstddef>

/**
 * Use #SharedLease with the #SharedAnchor base class to prevent items
 * from getting removed and freed while you are still using them.
 */
class CacheItem : public SharedAnchor {
	friend class Cache;

	/**
	 * This item's siblings, sorted by last access.
	 */
	IntrusiveListHook<IntrusiveHookMode::TRACK> sorted_siblings;

	IntrusiveHashSetHook<IntrusiveHookMode::NORMAL> set_hook;

	/**
	 * The key under which this item is stored in the hash table.
	 */
	const StringWithHash key;

	std::chrono::steady_clock::time_point expires;

	const size_t size;

public:
	CacheItem(StringWithHash _key, std::size_t _size,
		  std::chrono::steady_clock::time_point _expires) noexcept
		:key(_key), expires(_expires), size(_size) {}

	CacheItem(StringWithHash _key, std::size_t _size,
		  std::chrono::steady_clock::time_point now,
		  std::chrono::system_clock::time_point system_now,
		  std::chrono::system_clock::time_point _expires) noexcept;

	CacheItem(StringWithHash _key, std::size_t _size,
		  std::chrono::steady_clock::time_point now,
		  std::chrono::seconds max_age) noexcept;

	CacheItem(const CacheItem &) = delete;

	/**
	 * If true, then this item has been removed from the cache, but
	 * could not be destroyed yet, because it is locked.
	 */
	bool IsRemoved() const noexcept {
		return !sorted_siblings.is_linked();
	}

	void Release() noexcept {
		if (IsAbandoned())
			Destroy();
	}

	StringWithHash GetKey() const noexcept {
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

	struct GetKeyFunction {
		[[gnu::pure]]
		StringWithHash operator()(const CacheItem &item) const noexcept {
			return item.GetKey();
		}
	};

protected:
	/* virtual methods from SharedAnchor */
	void OnAbandoned() noexcept override;
};
