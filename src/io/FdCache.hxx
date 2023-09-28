// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/CoarseTimerEvent.hxx"
#include "util/BindMethod.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"

#include <cstdint>
#include <string_view>

struct open_how;
class FileDescriptor;
class SharedLease;
class CancellablePointer;
namespace Uring { class Queue; }

/**
 * A cache for file descriptors.
 */
class FdCache {
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
		Key operator()(const Item &item) noexcept;
	};

	CoarseTimerEvent expire_timer;

#ifdef HAVE_URING
	Uring::Queue *const uring_queue;
#endif // HAVE_URING

	IntrusiveHashSet<Item, 8192,
			 IntrusiveHashSetOperators<Key::Hash, std::equal_to<Key>,
						   ItemGetKey>> map;

	IntrusiveList<Item> chronological_list;

	bool enabled = true;

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
	 * Disable the cache, initiating shutdown.
	 */
	void Disable() noexcept;

	using SuccessCallback = BoundMethod<void(FileDescriptor fd, SharedLease lease) noexcept>;
	using ErrorCallback = BoundMethod<void(int error) noexcept>;

	/**
	 * Open a file (asynchronously).
	 *
	 * @param directory an optional directory descriptor (only
	 * used on cache miss)
	 *
	 * @param path an absolute path
	 *
	 * @param on_success the callback to be used on success
	 * @param on_error the callback to be used on error
	 */
	void Get(FileDescriptor beneath_directory,
		 std::string_view path,
		 const struct open_how &how,
		 SuccessCallback on_success,
		 ErrorCallback on_error,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	void Expire() noexcept;
};
