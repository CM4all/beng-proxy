// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/net/ServerSocket.hxx"
#include "io/uring/config.h" // for HAVE_URING
#include "util/IntrusiveList.hxx"

#include <memory>

class PoolPtr;
template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SslFactory;
class SslFilter;

#ifdef HAVE_URING
namespace Uring { class Queue; }
#endif

class FilteredSocketListenerHandler {
public:
	/**
	 * This method allows the handler to intercept a new
	 * connection that was just accepted, before doing any SSL/TLS
	 * handshake.  If it returns an undefined
	 * #UniqueSocketDescriptor, the connection will be discarded
	 * (though the socket can be used by the handler).
	 *
	 * Exceptions thrown by this method will be passed to
	 * OnFilteredSocketError().
	 */
	virtual UniqueSocketDescriptor OnFilteredSocketAccept(UniqueSocketDescriptor s,
							      SocketAddress address);

	virtual void OnFilteredSocketConnect(PoolPtr pool,
					     UniquePoolPtr<FilteredSocket> socket,
					     SocketAddress address,
					     const SslFilter *ssl_filter) noexcept = 0;
	virtual void OnFilteredSocketError(std::exception_ptr e) noexcept = 0;
};

/**
 * Listener on a TCP port which gives a #FilteredSocket to its handler.
 */
class FilteredSocketListener final : public ServerSocket {
	struct pool &parent_pool;

	std::unique_ptr<SslFactory> ssl_factory;

#ifdef HAVE_URING
	Uring::Queue *const uring_queue;
#endif

	FilteredSocketListenerHandler &handler;

	class Pending;
	IntrusiveList<Pending> pending;

public:
	FilteredSocketListener(struct pool &_pool, EventLoop &event_loop,
			       std::unique_ptr<SslFactory> _ssl_factory,
#ifdef HAVE_URING
			       Uring::Queue *uring_queue,
#endif
			       FilteredSocketListenerHandler &_handler,
			       UniqueSocketDescriptor _socket) noexcept;
	~FilteredSocketListener() noexcept;

protected:
	void OnAccept(UniqueSocketDescriptor s,
		      SocketAddress address) noexcept override;
	void OnAcceptError(std::exception_ptr e) noexcept override;
};
