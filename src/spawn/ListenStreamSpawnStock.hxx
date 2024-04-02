// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveHashSet.hxx"

#include <cstddef>
#include <string_view>

struct MountNamespaceOptions;
class AllocatorPtr;
class EventLoop;
class SharedLease;
class SpawnService;
class TranslationService;

/**
 * Manages stream listener sockets and when one becomes ready (because
 * a client connects to it), consults the translation server and spawn
 * a process which gets the listener socket on stdin.
 *
 * @see TranslationCommand::MOUNT_LISTEN_STREAM
 */
class ListenStreamSpawnStock {
	EventLoop &event_loop;
	TranslationService &translation_service;
	SpawnService &spawn_service;

	class Item;

	struct ItemHash {
		[[gnu::pure]]
		std::size_t operator()(std::string_view key) const noexcept;
	};

	struct ItemGetKey {
		[[gnu::const]]
		std::string_view operator()(const Item &item) const noexcept;
	};

	IntrusiveHashSet<Item, 1024,
			 IntrusiveHashSetOperators<Item, ItemGetKey, ItemHash,
						   std::equal_to<std::string_view>>> items;

public:
	ListenStreamSpawnStock(EventLoop &_event_loop,
			       TranslationService &_translation_service,
			       SpawnService &_spawn_service) noexcept;

	~ListenStreamSpawnStock() noexcept;

	void FadeAll() noexcept;
	void FadeTag(std::string_view tag) noexcept;

	/**
	 * Create a temporary directory containing a listener socket.
	 *
	 * Throws on error.
	 *
	 * @param key the path inside the container (this class uses
	 * only the last path component); optionally, an opaque tag
	 * may be followed, separated by a null byte
	 *
	 * @return the absolute path of the socket and a lease which
	 * shall be released when the socket is no longer needed (and
	 * all related processes can be terminated)
	 */
	[[nodiscard]]
	std::pair<const char *, SharedLease> Get(std::string_view key);

	/**
	 * Replace the #mount_listen_stream field (if set) with a
	 * #mounts item.
	 *
	 * Throws on error.
	 *
	 * @return a lease (same as in the Get() return value)
	 */
	[[nodiscard]]
	SharedLease Apply(AllocatorPtr alloc, MountNamespaceOptions &mount_ns);
};
