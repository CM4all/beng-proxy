// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "cache/Cache.hxx"
#include "stats/CacheStats.hxx"
#include "memory/Rubber.hxx"
#include "event/FarTimerEvent.hxx"
#include "util/IntrusiveList.hxx"

class UnusedIstreamPtr;

class EncodingCache final {
	static constexpr Event::Duration compress_interval = std::chrono::minutes(10);

	Rubber rubber;
	Cache cache;

	FarTimerEvent compress_timer;

	struct Item;

	class Store;

	IntrusiveList<Store> stores;

	mutable CacheStats stats{};

public:
	EncodingCache(EventLoop &_event_loop, std::size_t max_size);

	~EncodingCache() noexcept;

	auto &GetEventLoop() const noexcept {
		return compress_timer.GetEventLoop();
	}

	void ForkCow(bool inherit) noexcept {
		rubber.ForkCow(inherit);
	}

	CacheStats GetStats() const noexcept {
		stats.allocator = rubber.GetStats();
		return stats;
	}

	void Flush() noexcept {
		cache.Flush();
		Compress();
	}

	UnusedIstreamPtr Get(struct pool &pool, StringWithHash key) noexcept;

	UnusedIstreamPtr Put(struct pool &pool, StringWithHash key,
			     UnusedIstreamPtr src) noexcept;

private:
	void Add(StringWithHash key,
		 RubberAllocation &&a, std::size_t size) noexcept;

	void Compress() noexcept {
		rubber.Compress();
	}

	void OnCompressTimer() noexcept {
		Compress();
		compress_timer.Schedule(compress_interval);
	}
};
