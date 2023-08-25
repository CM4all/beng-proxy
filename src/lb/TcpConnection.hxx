// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "fs/FilteredSocket.hxx"
#include "cluster/StickyHash.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "event/DeferEvent.hxx"
#include "io/Logger.hxx"
#include "event/net/ConnectSocket.hxx"
#include "net/StaticSocketAddress.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"
#include "util/IntrusiveList.hxx"

#include <exception>

class UniqueSocketDescriptor;
class SocketAddress;
struct LbListenerConfig;
class LbCluster;
struct LbInstance;

class LbTcpConnection final
	: PoolHolder, LoggerDomainFactory, ConnectSocketHandler,
	  public IntrusiveListHook<IntrusiveHookMode::NORMAL>
{
	LbInstance &instance;

	const LbListenerConfig &listener;
	LbCluster &cluster;

	/**
	 * The client's address formatted as a string (for logging).  This
	 * is guaranteed to be non-nullptr.
	 */
	const char *client_address;

	const sticky_hash_t sticky_hash;

	const LazyDomainLogger logger;

public:
	struct Inbound final : BufferedSocketHandler {
		UniquePoolPtr<FilteredSocket> socket;

		explicit Inbound(UniquePoolPtr<FilteredSocket> &&_socket) noexcept;

	private:
		/* virtual methods from class BufferedSocketHandler */
		BufferedResult OnBufferedData() override;
		// TODO: DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
		bool OnBufferedHangup() noexcept override;
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

		explicit Outbound(EventLoop &event_loop) noexcept
			:socket(event_loop) {}

	private:
		/* virtual methods from class BufferedSocketHandler */
		BufferedResult OnBufferedData() override;
		// TODO: DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
		bool OnBufferedClosed() noexcept override;
		bool OnBufferedEnd() override;
		bool OnBufferedWrite() override;
		enum write_result OnBufferedBroken() noexcept override;
		void OnBufferedError(std::exception_ptr e) noexcept override;
	} outbound;

	static constexpr LbTcpConnection &FromOutbound(Outbound &o) noexcept {
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
			UniquePoolPtr<FilteredSocket> &&_socket,
			SocketAddress _client_address) noexcept;

	~LbTcpConnection() noexcept;

	static LbTcpConnection *New(LbInstance &instance,
				    const LbListenerConfig &listener,
				    LbCluster &cluster,
				    PoolPtr pool,
				    UniquePoolPtr<FilteredSocket> socket,
				    SocketAddress address) noexcept;

	auto &GetEventLoop() const noexcept {
		return outbound.socket.GetEventLoop();
	}

	void Destroy() noexcept;

protected:
	/* virtual methods from class LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override;

private:
	void ConnectOutbound() noexcept;

public:
	void OnDeferredHandshake() noexcept;

	void OnTcpEnd() noexcept;
	void OnTcpError(const char *prefix, const char *error) noexcept;
	void OnTcpErrno(const char *prefix, int error) noexcept;
	void OnTcpError(const char *prefix, std::exception_ptr ep) noexcept;

private:
	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;
	void OnSocketConnectTimeout() noexcept override;
	void OnSocketConnectError(std::exception_ptr ep) noexcept override;
};
