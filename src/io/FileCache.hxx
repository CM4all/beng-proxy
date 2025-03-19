// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/InotifyManager.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/StringWithHash.hxx"

#include <cstddef>
#include <span>

class SharedLease;

/**
 * A cache for file contents (of small files).  This is used to cache
 * the READ_FILE translation packet.
 *
 * There is no expiry, other than inotify.  Unused items are never
 * removed (unless the file is modified/deleted/moved).  This cache is
 * meant for small numbers of files.  Time-based expiry would need to
 * be implemented if many files are used for the READ_FILE featrure.
 */
class FileCache final {
	struct Item;

	struct ItemGetKey {
		[[gnu::pure]]
		StringWithHash operator()(const Item &item) const noexcept;
	};

	InotifyManager inotify_manager;

	IntrusiveHashSet<Item, 8192,
			 IntrusiveHashSetOperators<Item, ItemGetKey,
						   std::hash<StringWithHash>,
						   std::equal_to<StringWithHash>>> map;

public:
	explicit FileCache(EventLoop &event_loop) noexcept;
	~FileCache() noexcept;

	auto &GetEventLoop() const noexcept {
		return inotify_manager.GetEventLoop();
	}

	/**
	 * Clear the cache.
	 */
	void Flush() noexcept;

	/**
	 * Initiate shutdown.  This unregisters all #EventLoop events
	 * and prevents new ones from getting registered.
	 */
	void BeginShutdown() noexcept;

	/**
	 * Has BeginShutdown() been called?
	 */
	bool IsShuttingDown() const noexcept {
		return inotify_manager.IsShuttingDown();
	}

	/**
	 * Get the contents of the specified file.  Returns
	 * nullptr/nullptr on error.
	 */
	std::pair<std::span<const std::byte>, SharedLease> Get(const char *path,
							       std::size_t max_size) noexcept;
};
