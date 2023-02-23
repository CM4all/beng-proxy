// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Connection.hxx"
#include "RLogger.hxx"
#include "Request.hxx"
#include "Instance.hxx"
#include "Listener.hxx"
#include "http/IncomingRequest.hxx"
#include "http/server/Public.hxx"
#include "http/server/Handler.hxx"
#include "http/server/Error.hxx"
#include "drop.hxx"
#include "address_string.hxx"
#include "pool/UniquePtr.hxx"
#include "fs/FilteredSocket.hxx"
#include "ssl/AlpnCompare.hxx"
#include "ssl/Filter.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/TimeoutError.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"
#include "pool/pool.hxx"

#ifdef HAVE_NGHTTP2
#include "nghttp2/Server.hxx"
#endif

#include <assert.h>

BpConnection::BpConnection(PoolPtr &&_pool, BpInstance &_instance,
			   BPListener &_listener,
			   SocketAddress remote_address,
			   const SslFilter *ssl_filter) noexcept
	:PoolHolder(std::move(_pool)),
	 instance(_instance),
	 listener(_listener),
	 config(_instance.config),
	 remote_host_and_port(address_to_string(pool, remote_address)),
	 logger(remote_host_and_port != nullptr
		? remote_host_and_port
		: "unknown"),
	 peer_subject(ssl_filter != nullptr
		      ? ssl_filter_get_peer_subject(*ssl_filter)
		      : nullptr),
	 peer_issuer_subject(ssl_filter != nullptr
			     ? ssl_filter_get_peer_issuer_subject(*ssl_filter)
			     : nullptr),
	 ssl(ssl_filter != nullptr)
{
}

BpConnection::~BpConnection() noexcept
{
	if (http != nullptr)
		http_server_connection_close(http);

	pool_trash(pool);
}

void
BpConnection::Disposer::operator()(BpConnection *c) noexcept
{
	c->~BpConnection();
}

void
close_connection(BpConnection *connection) noexcept
{
	auto &connections = connection->instance.connections;
	assert(!connections.empty());
	connections.erase_and_dispose(connections.iterator_to(*connection),
				      BpConnection::Disposer());
}

[[gnu::pure]]
static int
HttpServerLogLevel(std::exception_ptr e) noexcept
{
	try {
		FindRetrowNested<HttpServerSocketError>(e);
	} catch (const HttpServerSocketError &) {
		e = std::current_exception();

		/* some socket errors caused by our client are less
		   important */

		if (const auto *se = FindNested<std::system_error>(e);
		    se != nullptr && IsErrno(*se, ECONNRESET))
			return 4;

		if (FindNested<SocketProtocolError>(e) ||
		    FindNested<TimeoutError>(e))
			return 4;
	}

	return 2;
}


/*
 * http connection handler
 *
 */

void
BpConnection::RequestHeadersFinished(IncomingHttpRequest &request) noexcept
{
	request.logger = NewFromPool<BpRequestLogger>(request.pool, instance,
						      listener.GetHttpStats());
}

void
BpConnection::HandleHttpRequest(IncomingHttpRequest &request,
				const StopwatchPtr &parent_stopwatch,
				CancellablePointer &cancel_ptr) noexcept
{
	auto *request2 = NewFromPool<Request>(request.pool,
					      *this, request,
					      parent_stopwatch);
	request2->HandleHttpRequest(cancel_ptr);
}

void
BpConnection::HttpConnectionError(std::exception_ptr e) noexcept
{
	http = nullptr;

	logger(HttpServerLogLevel(e), e);

	close_connection(this);
}

void
BpConnection::HttpConnectionClosed() noexcept
{
	http = nullptr;

	close_connection(this);
}

/*
 * listener handler
 *
 */

void
new_connection(PoolPtr pool, BpInstance &instance, BPListener &listener,
	       HttpServerRequestHandler *request_handler,
	       UniquePoolPtr<FilteredSocket> socket,
	       const SslFilter *ssl_filter,
	       SocketAddress address) noexcept
{
	if (instance.connections.size() >= instance.config.max_connections) {
		unsigned num_dropped = drop_some_connections(&instance);

		if (num_dropped == 0) {
			LogConcat(1, "connection", "too many connections (",
				  unsigned(instance.connections.size()),
				  ", dropping");
			return;
		}
	}

	/* determine the local socket address */
	const auto local_address = socket->GetSocket().GetLocalAddress();

	auto *connection = NewFromPool<BpConnection>(std::move(pool), instance,
						     listener,
						     address, ssl_filter);
	instance.connections.push_front(*connection);

	if (request_handler == nullptr)
		request_handler = connection;

#ifdef HAVE_NGHTTP2
	if (IsAlpnHttp2(ssl_filter))
		connection->http2 = UniquePoolPtr<NgHttp2::ServerConnection>::Make(connection->GetPool(),
										   connection->GetPool(),
										   std::move(socket),
										   address,
										   *connection,
										   *request_handler);
	else
#endif
		connection->http =
			http_server_connection_new(connection->GetPool(),
						   std::move(socket),
						   local_address.IsDefined()
						   ? (SocketAddress)local_address
						   : nullptr,
						   address,
						   true,
						   *connection,
						   *request_handler);
}
