/*
 * Copyright 2007-2017 Content Management AG
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

#ifndef BENG_PROXY_LB_CONNECTION_H
#define BENG_PROXY_LB_CONNECTION_H

#include "fs/FilteredSocket.hxx"
#include "StickyHash.hxx"
#include "pool/Holder.hxx"
#include "event/DeferEvent.hxx"
#include "io/Logger.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/PConnectSocket.hxx"
#include "io/FdType.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"

#include <boost/intrusive/list.hpp>

#include <exception>

#include <stdint.h>

struct SslFactory;
struct SslFilter;
class UniqueSocketDescriptor;
class SocketAddress;
struct LbListenerConfig;
class LbCluster;
struct LbGoto;
struct LbInstance;

class LbTcpConnection final
	: PoolHolder, LoggerDomainFactory, ConnectSocketHandler,
	  public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

	LbInstance &instance;

	const LbListenerConfig &listener;
	LbCluster &cluster;

	/**
	 * The client's address formatted as a string (for logging).  This
	 * is guaranteed to be non-nullptr.
	 */
	const char *client_address;

	const sticky_hash_t session_sticky;

	const LazyDomainLogger logger;

public:
	struct Inbound final : BufferedSocketHandler {
		FilteredSocket socket;

		Inbound(EventLoop &event_loop,
			UniqueSocketDescriptor &&fd, FdType fd_type,
			SocketFilterPtr &&filter);

		void Destroy();

		void ScheduleHandshakeCallback(BoundMethod<void() noexcept> callback) {
			socket.ScheduleReadNoTimeout(false);
			socket.SetHandshakeCallback(callback);
		}

	private:
		/* virtual methods from class BufferedSocketHandler */
		BufferedResult OnBufferedData() override;
		// TODO: DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
		bool OnBufferedClosed() noexcept override;
		bool OnBufferedWrite() override;
		bool OnBufferedDrained() noexcept override;
		enum write_result OnBufferedBroken() noexcept override;
		void OnBufferedError(std::exception_ptr e) noexcept override;
	} inbound;

	static constexpr LbTcpConnection &FromInbound(Inbound &i) {
		return ContainerCast(i, &LbTcpConnection::inbound);
	}

	struct Outbound final : BufferedSocketHandler {
		BufferedSocket socket;

		explicit Outbound(EventLoop &event_loop)
			:socket(event_loop) {}

		void Destroy();

	private:
		/* virtual methods from class BufferedSocketHandler */
		BufferedResult OnBufferedData() override;
		// TODO: DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
		bool OnBufferedClosed() noexcept override;
		bool OnBufferedEnd() noexcept override;
		bool OnBufferedWrite() override;
		enum write_result OnBufferedBroken() noexcept override;
		void OnBufferedError(std::exception_ptr e) noexcept override;
	} outbound;

	static constexpr LbTcpConnection &FromOutbound(Outbound &o) {
		return ContainerCast(o, &LbTcpConnection::outbound);
	}

	StaticSocketAddress bind_address;

	/**
	 * This class defers the connect to the outbound server, to move
	 * it out of the OnHandshake() stack frame, to avoid destroing the
	 * caller's object.
	 */
	DeferEvent defer_connect;

	CancellablePointer cancel_connect;

	bool got_inbound_data, got_outbound_data;

	LbTcpConnection(PoolPtr &&pool, LbInstance &_instance,
			const LbListenerConfig &_listener,
			LbCluster &_cluster,
			UniqueSocketDescriptor &&fd, FdType fd_type,
			SocketFilterPtr &&filter,
			SocketAddress _client_address);

	~LbTcpConnection();

	static LbTcpConnection *New(LbInstance &instance,
				    const LbListenerConfig &listener,
				    LbCluster &cluster,
				    SslFactory *ssl_factory,
				    UniqueSocketDescriptor &&fd,
				    SocketAddress address);

	EventLoop &GetEventLoop() {
		return inbound.socket.GetEventLoop();
	}

	void Destroy();

protected:
	/* virtual methods from class LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override;

private:
	void ScheduleHandshakeCallback() {
		inbound.ScheduleHandshakeCallback(BIND_THIS_METHOD(OnHandshake));
	}

	void ConnectOutbound();

public:
	void OnHandshake() noexcept {
		defer_connect.Schedule();
	}

	void OnDeferredHandshake() noexcept;

	void OnTcpEnd();
	void OnTcpError(const char *prefix, const char *error);
	void OnTcpErrno(const char *prefix, int error);
	void OnTcpError(const char *prefix, std::exception_ptr ep);

private:
	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept override;
	void OnSocketConnectTimeout() noexcept override;
	void OnSocketConnectError(std::exception_ptr ep) noexcept override;
};

#endif
