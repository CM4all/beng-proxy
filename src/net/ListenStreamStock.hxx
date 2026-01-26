// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/IntrusiveHashSet.hxx"
#include "util/TransparentHash.hxx"

#include <cstddef>
#include <exception>
#include <string_view>

struct MountNamespaceOptions;
class AllocatorPtr;
class EventLoop;
class SharedLease;
class DisposablePointer;
class CancellablePointer;
class SocketDescriptor;

class ListenStreamReadyHandler {
public:
	virtual void OnListenStreamSuccess(DisposablePointer server,
					   std::string_view tags) noexcept = 0;
	virtual void OnListenStreamError(std::exception_ptr error) noexcept = 0;
	virtual void OnListenStreamExit() noexcept = 0;
};

class ListenStreamStockHandler {
public:
	virtual void OnListenStreamReady(std::string_view key,
					 const char *socket_path,
					 SocketDescriptor socket,
					 ListenStreamReadyHandler &handler,
					 CancellablePointer &cancel_ptr) noexcept = 0;
};

/**
 * Manages stream listener sockets and when one becomes ready (because
 * a client connects to it), consults the translation server and spawn
 * a process which gets the listener socket on stdin.
 *
 * @see TranslationCommand::MOUNT_LISTEN_STREAM
 */
class ListenStreamStock {
	EventLoop &event_loop;
	ListenStreamStockHandler &handler;

	class Item;

	struct ItemGetKey {
		[[gnu::const]]
		std::string_view operator()(const Item &item) const noexcept;
	};

	IntrusiveHashSet<Item, 1024,
			 IntrusiveHashSetOperators<Item, ItemGetKey, TransparentHash,
						   std::equal_to<std::string_view>>> items;

public:
	ListenStreamStock(EventLoop &_event_loop,
			  ListenStreamStockHandler &_handler) noexcept;

	~ListenStreamStock() noexcept;

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
