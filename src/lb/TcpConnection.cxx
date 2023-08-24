// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TcpConnection.hxx"
#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "ListenerConfig.hxx"
#include "Instance.hxx"
#include "AllocatorPtr.hxx"
#include "cluster/AddressSticky.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "address_string.hxx"

#include <assert.h>

static constexpr Event::Duration LB_TCP_CONNECT_TIMEOUT =
	std::chrono::seconds(20);

static constexpr auto write_timeout = std::chrono::seconds(30);

[[gnu::pure]]
static sticky_hash_t
lb_tcp_sticky(StickyMode sticky_mode,
	      SocketAddress remote_address)
{
	switch (sticky_mode) {
	case StickyMode::NONE:
	case StickyMode::FAILOVER:
		break;

	case StickyMode::SOURCE_IP:
		return socket_address_sticky(remote_address);

	case StickyMode::HOST:
	case StickyMode::XHOST:
	case StickyMode::SESSION_MODULO:
	case StickyMode::COOKIE:
	case StickyMode::JVM_ROUTE:
		/* not implemented here */
		break;
	}

	return 0;
}

/*
 * inbound BufferedSocketHandler
 *
 */

BufferedResult
LbTcpConnection::Inbound::OnBufferedData()
{
	auto &tcp = LbTcpConnection::FromInbound(*this);

	tcp.got_inbound_data = true;

	if (tcp.defer_connect.IsPending() || tcp.cancel_connect)
		/* outbound is not yet connected */
		return BufferedResult::OK;

	if (!tcp.outbound.socket.IsValid()) {
		tcp.OnTcpError("Send error", "Broken socket");
		return BufferedResult::DESTROYED;
	}

	auto r = socket->ReadBuffer();
	assert(!r.empty());

	ssize_t nbytes = tcp.outbound.socket.Write(r.data(), r.size());
	if (nbytes > 0) {
		tcp.outbound.socket.ScheduleWrite();
		socket->DisposeConsumed(nbytes);
		return BufferedResult::OK;
	}

	switch ((enum write_result)nbytes) {
		int save_errno;

	case WRITE_SOURCE_EOF:
		assert(false);
		gcc_unreachable();

	case WRITE_ERRNO:
		save_errno = errno;
		tcp.OnTcpErrno("Send failed", save_errno);
		return BufferedResult::DESTROYED;

	case WRITE_BLOCKING:
		return BufferedResult::OK;

	case WRITE_DESTROYED:
		return BufferedResult::DESTROYED;

	case WRITE_BROKEN:
		tcp.OnTcpEnd();
		return BufferedResult::DESTROYED;
	}

	assert(false);
	gcc_unreachable();
}

bool
LbTcpConnection::Inbound::OnBufferedHangup() noexcept
{
	auto &tcp = LbTcpConnection::FromInbound(*this);

	tcp.OnTcpEnd();
	return false;
}

bool
LbTcpConnection::Inbound::OnBufferedClosed() noexcept
{
	auto &tcp = LbTcpConnection::FromInbound(*this);

	tcp.OnTcpEnd();
	return false;
}

bool
LbTcpConnection::Inbound::OnBufferedWrite()
{
	auto &tcp = LbTcpConnection::FromInbound(*this);

	tcp.got_outbound_data = false;

	switch (tcp.outbound.socket.Read()) {
	case BufferedReadResult::OK:
	case BufferedReadResult::BLOCKING:
		break;

	case BufferedReadResult::DISCONNECTED:
	case BufferedReadResult::DESTROYED:
		return false;
	}

	if (!tcp.got_outbound_data)
		socket->UnscheduleWrite();
	return true;
}

bool
LbTcpConnection::Inbound::OnBufferedDrained() noexcept
{
	auto &tcp = LbTcpConnection::FromInbound(*this);

	if (!tcp.outbound.socket.IsValid()) {
		/* now that inbound's output buffers are drained, we can
		   finally close the connection (postponed from
		   outbound_buffered_socket_end()) */
		tcp.OnTcpEnd();
		return false;
	}

	return true;
}

enum write_result
LbTcpConnection::Inbound::OnBufferedBroken() noexcept
{
	auto &tcp = LbTcpConnection::FromInbound(*this);

	tcp.OnTcpEnd();
	return WRITE_DESTROYED;
}

void
LbTcpConnection::Inbound::OnBufferedError(std::exception_ptr ep) noexcept
{
	auto &tcp = LbTcpConnection::FromInbound(*this);

	tcp.OnTcpError("Error", ep);
}

/*
 * outbound buffered_socket_handler
 *
 */

BufferedResult
LbTcpConnection::Outbound::OnBufferedData()
{
	auto &tcp = LbTcpConnection::FromOutbound(*this);

	tcp.got_outbound_data = true;

	auto r = socket.ReadBuffer();
	assert(!r.empty());

	ssize_t nbytes = tcp.inbound.socket->Write(r);
	if (nbytes > 0) {
		tcp.inbound.socket->ScheduleWrite();
		socket.DisposeConsumed(nbytes);
		return BufferedResult::OK;
	}

	switch ((enum write_result)nbytes) {
		int save_errno;

	case WRITE_SOURCE_EOF:
		assert(false);
		gcc_unreachable();

	case WRITE_ERRNO:
		save_errno = errno;
		tcp.OnTcpErrno("Send failed", save_errno);
		return BufferedResult::DESTROYED;

	case WRITE_BLOCKING:
		return BufferedResult::OK;

	case WRITE_DESTROYED:
		return BufferedResult::DESTROYED;

	case WRITE_BROKEN:
		tcp.OnTcpEnd();
		return BufferedResult::DESTROYED;
	}

	assert(false);
	gcc_unreachable();
}

bool
LbTcpConnection::Outbound::OnBufferedClosed() noexcept
{
	socket.Close();
	return true;
}

bool
LbTcpConnection::Outbound::OnBufferedEnd() noexcept
{
	auto &tcp = LbTcpConnection::FromOutbound(*this);

	socket.Destroy();

	tcp.inbound.socket->UnscheduleWrite();

	if (tcp.inbound.socket->IsDrained()) {
		/* all output buffers to "inbound" are drained; close the
		   connection, because there's nothing left to do */
		tcp.OnTcpEnd();

		/* nothing will be done if the buffers are not yet drained;
		   we're waiting for inbound_buffered_socket_drained() to be
		   called */
	}

	return true;
}

bool
LbTcpConnection::Outbound::OnBufferedWrite()
{
	auto &tcp = LbTcpConnection::FromOutbound(*this);

	tcp.got_inbound_data = false;

	switch (tcp.inbound.socket->Read()) {
	case BufferedReadResult::OK:
	case BufferedReadResult::BLOCKING:
		break;

	case BufferedReadResult::DISCONNECTED:
	case BufferedReadResult::DESTROYED:
		return false;
	}

	if (!tcp.got_inbound_data)
		socket.UnscheduleWrite();
	return true;
}

enum write_result
LbTcpConnection::Outbound::OnBufferedBroken() noexcept
{
	auto &tcp = LbTcpConnection::FromOutbound(*this);

	tcp.OnTcpEnd();
	return WRITE_DESTROYED;
}

void
LbTcpConnection::Outbound::OnBufferedError(std::exception_ptr ep) noexcept
{
	auto &tcp = LbTcpConnection::FromOutbound(*this);

	tcp.OnTcpError("Error", ep);
}

std::string
LbTcpConnection::MakeLoggerDomain() const noexcept
{
	return "listener='" + listener.name
		+ "' cluster='" + listener.destination.GetName()
		+ "' client='" + client_address
		+ "'";
}

inline void
LbTcpConnection::OnDeferredHandshake() noexcept
{
	assert(!cancel_connect);
	assert(!outbound.socket.IsValid());

	ConnectOutbound();
}

void
LbTcpConnection::OnTcpEnd() noexcept
{
	Destroy();
}

void
LbTcpConnection::OnTcpError(const char *prefix, const char *error) noexcept
{
	logger(3, prefix, ": ", error);
	Destroy();
}

void
LbTcpConnection::OnTcpErrno(const char *prefix, int error) noexcept
{
	logger(3, prefix, ": ", strerror(error));
	Destroy();
}

void
LbTcpConnection::OnTcpError(const char *prefix, std::exception_ptr ep) noexcept
{
	logger(3, prefix, ": ", ep);
	Destroy();
}

/*
 * ConnectSocketHandler
 *
 */

void
LbTcpConnection::OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept
{
	cancel_connect = nullptr;

	outbound.socket.Init(fd.Release(), FdType::FD_TCP,
			     write_timeout, outbound);

	/* TODO
	   outbound.direct = pipe_stock != nullptr &&
	   (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0 &&
	   (istream_direct_mask_to(inbound.base.base.fd_type) & FdType::FD_PIPE) != 0;
	*/

	switch (inbound.socket->Read()) {
	case BufferedReadResult::OK:
	case BufferedReadResult::BLOCKING:
		outbound.socket.Read();
		break;

	case BufferedReadResult::DISCONNECTED:
	case BufferedReadResult::DESTROYED:
		break;
	}
}

void
LbTcpConnection::OnSocketConnectTimeout() noexcept
{
	cancel_connect = nullptr;

	OnTcpError("Connect error", "Timeout");
}

void
LbTcpConnection::OnSocketConnectError(std::exception_ptr ep) noexcept
{
	cancel_connect = nullptr;

	OnTcpError("Connect error", ep);
}

void
LbTcpConnection::ConnectOutbound() noexcept
{
	cluster.ConnectTcp(*pool, bind_address, sticky_hash,
			   LB_TCP_CONNECT_TIMEOUT,
			   *this, cancel_connect);
}

/*
 * public
 *
 */

inline
LbTcpConnection::Inbound::Inbound(UniquePoolPtr<FilteredSocket> &&_socket) noexcept
	:socket(std::move(_socket))
{
	socket->Reinit(write_timeout, *this);
	/* TODO
	   socket.base.direct = pipe_stock != nullptr &&
	   (ISTREAM_TO_PIPE & fd_type) != 0 &&
	   (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0;
	*/

}

inline
LbTcpConnection::LbTcpConnection(PoolPtr &&_pool, LbInstance &_instance,
				 const LbListenerConfig &_listener,
				 LbCluster &_cluster,
				 UniquePoolPtr<FilteredSocket> &&_socket,
				 SocketAddress _client_address) noexcept
	:PoolHolder(std::move(_pool)),
	 instance(_instance), listener(_listener), cluster(_cluster),
	 client_address(address_to_string(pool, _client_address)),
	 sticky_hash(lb_tcp_sticky(cluster.GetConfig().sticky_mode,
				   _client_address)),
	 logger(*this),
	 inbound(std::move(_socket)),
	 outbound(instance.event_loop),
	 defer_connect(instance.event_loop, BIND_THIS_METHOD(OnDeferredHandshake))
{
	if (client_address == nullptr)
		client_address = "unknown";

	if (cluster.GetConfig().transparent_source) {
		bind_address = _client_address;
		bind_address.SetPort(0);
	} else
		bind_address.Clear();

	instance.tcp_connections.push_back(*this);

	defer_connect.Schedule();
}

LbTcpConnection::~LbTcpConnection() noexcept
{
	if (cancel_connect) {
		cancel_connect.Cancel();
		cancel_connect = nullptr;
	}

	auto &connections = instance.tcp_connections;
	connections.erase(connections.iterator_to(*this));
}

LbTcpConnection *
LbTcpConnection::New(LbInstance &instance,
		     const LbListenerConfig &listener,
		     LbCluster &cluster,
		     PoolPtr pool,
		     UniquePoolPtr<FilteredSocket> socket,
		     SocketAddress address) noexcept
{
	assert(listener.destination.GetProtocol() == LbProtocol::TCP);

	return NewFromPool<LbTcpConnection>(std::move(pool), instance,
					    listener, cluster,
					    std::move(socket),
					    address);
}

void
LbTcpConnection::Destroy() noexcept
{
	assert(!instance.tcp_connections.empty());
	assert(listener.destination.GetProtocol() == LbProtocol::TCP);

	this->~LbTcpConnection();
}
