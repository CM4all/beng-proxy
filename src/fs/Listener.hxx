/*
 * Copyright 2007-2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "event/net/ServerSocket.hxx"
#include "util/IntrusiveList.hxx"

#include <memory>

class PoolPtr;
template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SslFactory;
class SslFilter;

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

	FilteredSocketListenerHandler &handler;

	class Pending;
	IntrusiveList<Pending> pending;

public:
	FilteredSocketListener(struct pool &_pool, EventLoop &event_loop,
			       std::unique_ptr<SslFactory> _ssl_factory,
			       FilteredSocketListenerHandler &_handler) noexcept;
	~FilteredSocketListener() noexcept;

protected:
	void OnAccept(UniqueSocketDescriptor &&s,
		      SocketAddress address) noexcept override;
	void OnAcceptError(std::exception_ptr e) noexcept override;
};
