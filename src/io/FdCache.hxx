// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/CoarseTimerEvent.hxx"
#include "event/InotifyManager.hxx"
#include "util/BindMethod.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"

#include "io/uring/config.h" // for HAVE_URING

#include <cstdint>
#include <string_view>

struct open_how;
struct statx;
class FileDescriptor;
class SharedLease;
class CancellablePointer;

#ifdef HAVE_URING
namespace Uring { class Queue; }
#endif

/**
 * A cache for file descriptors.
 */
class FdCache final {
	struct Key {
		std::string_view path;
		uint_least64_t flags;

		friend constexpr auto operator<=>(const Key &,
						  const Key &) noexcept = default;

		struct Hash {
			[[gnu::pure]]
			std::size_t operator()(const Key &key) const noexcept;
		};
	};

	struct Item;

	struct ItemGetKey {
		[[gnu::pure]]
		Key operator()(const Item &item) const noexcept;
	};

	CoarseTimerEvent expire_timer;

#ifdef HAVE_URING
	Uring::Queue *const uring_queue;
#endif // HAVE_URING

	InotifyManager inotify_manager;

	/**
	 * Map #Key (path and flags) to #Item.
	 */
	IntrusiveHashSet<Item, 8192,
			 IntrusiveHashSetOperators<Item, ItemGetKey,
						   Key::Hash, std::equal_to<Key>>> map;

	/**
	 * A list of items sorted by its "expires" field.  This is
	 * used by Expire().
	 */
	IntrusiveList<Item> chronological_list;

public:
	FdCache(EventLoop &event_loop
#ifdef HAVE_URING
		, Uring::Queue *uring_queue
#endif // HAVE_URING
		) noexcept;
	~FdCache() noexcept;

	auto &GetEventLoop() const noexcept {
		return expire_timer.GetEventLoop();
	}

	bool empty() const noexcept {
		return chronological_list.empty();
	}

	/**
	 * Close all open file descriptors as soon as they are unused.
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

	using SuccessCallback = BoundMethod<void(FileDescriptor fd, const struct statx &stx,
						 SharedLease &&lease) noexcept>;
	using ErrorCallback = BoundMethod<void(int error) noexcept>;

	/**
	 * Open a file (asynchronously).
	 *
	 * @param directory an optional directory descriptor (only
	 * used on cache miss)
	 *
	 * @param strip_path the absolute path of the #directory
	 * parameter; it is stripped from the #path parameter
	 * (necessary if using RESOLVE_BENEATH)
	 *
	 * @param path an absolute path (must be normalized)
	 *
	 * @param stx_mask if non-zero, then statx() is called,
	 * collecting information about the file descriptor
	 *
	 * @param on_success the callback to be used on success
	 * @param on_error the callback to be used on error
	 */
	void Get(FileDescriptor directory,
		 std::string_view strip_path,
		 std::string_view path,
		 const struct open_how &how,
		 unsigned stx_mask,
		 SuccessCallback on_success,
		 ErrorCallback on_error,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	/**
	 * Reduce the "expires" time of the given item, also changing
	 * its position in #chronological_list.
	 */
	void SetExpiresSoon(Item &item, Event::Duration expiry) noexcept;

	void Expire() noexcept;
};
