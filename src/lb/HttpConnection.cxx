// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "HttpConnection.hxx"
#include "RLogger.hxx"
#include "Config.hxx"
#include "Check.hxx"
#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "ListenerConfig.hxx"
#include "Goto.txx"
#include "ForwardHttpRequest.hxx"
#include "DelayForwardHttpRequest.hxx"
#include "Instance.hxx"
#include "Listener.hxx"
#include "http/IncomingRequest.hxx"
#include "http/server/Public.hxx"
#include "http/server/Handler.hxx"
#include "http/server/Error.hxx"
#include "pool/pool.hxx"
#include "fs/FilteredSocket.hxx"
#include "ssl/AlpnCompare.hxx"
#include "uri/Verify.hxx"
#include "lib/fmt/SocketAddressFormatter.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/TimeoutError.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"
#include "HttpMessageResponse.hxx"
#include "AllocatorPtr.hxx"

#ifdef HAVE_NGHTTP2
#include "nghttp2/Server.hxx"
#endif

#include <assert.h>

inline
LbHttpConnection::LbHttpConnection(PoolPtr &&_pool, LbInstance &_instance,
				   LbListener &_listener,
				   const LbGoto &_destination,
				   SocketAddress _client_address) noexcept
	:PoolHolder(std::move(_pool)), instance(_instance),
	 listener(_listener),
	 listener_config(listener.GetConfig()),
	 initial_destination(_destination),
	 client_address(_client_address),
	 logger(*this)
{
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
 * public
 *
 */

LbHttpConnection *
NewLbHttpConnection(LbInstance &instance,
		    LbListener &listener,
		    const LbGoto &destination,
		    PoolPtr pool,
		    UniquePoolPtr<FilteredSocket> socket,
		    const SslFilter *ssl_filter,
		    SocketAddress address) noexcept
{
	assert(listener.GetProtocol() == LbProtocol::HTTP);

	/* determine the local socket address */
	StaticSocketAddress local_address = socket->GetSocket().GetLocalAddress();

	auto *connection = NewFromPool<LbHttpConnection>(std::move(pool), instance,
							 listener, destination,
							 address);
	connection->ssl_filter = ssl_filter;

	instance.http_connections.push_back(*connection);

#ifdef HAVE_NGHTTP2
	if (listener.GetConfig().force_http2 || IsAlpnHttp2(ssl_filter))
		connection->http2 = UniquePoolPtr<NgHttp2::ServerConnection>::Make(connection->GetPool(),
										   connection->GetPool(),
										   std::move(socket),
										   address,
										   *connection,
										   *connection);
	else
#endif
		connection->http = http_server_connection_new(connection->GetPool(),
							      std::move(socket),
							      local_address.IsDefined()
							      ? (SocketAddress)local_address
							      : nullptr,
							      address,
							      false,
							      *connection,
							      *connection);

	return connection;
}

void
LbHttpConnection::Destroy() noexcept
{
	assert(!instance.http_connections.empty());

	auto &connections = instance.http_connections;
	connections.erase(connections.iterator_to(*this));

	this->~LbHttpConnection();
}

void
LbHttpConnection::CloseAndDestroy() noexcept
{
	assert(listener.GetProtocol() == LbProtocol::HTTP);
#ifdef HAVE_NGHTTP2
	assert(http != nullptr || http2);
#endif

	if (http != nullptr)
		http_server_connection_close(http);

	Destroy();
}

void
LbHttpConnection::SendError(IncomingHttpRequest &request, std::exception_ptr ep) noexcept
{
	if (const auto *r = FindNested<HttpMessageResponse>(ep)) {
		request.SendMessage(r->GetStatus(),
				    p_strdup(request.pool, r->what()));
		return;
	}

	const char *msg = listener_config.verbose_response
		? p_strdup(request.pool, GetFullMessage(ep).c_str())
		: "Bad gateway";

	request.SendMessage(HttpStatus::BAD_GATEWAY, msg);
}

void
LbHttpConnection::LogSendError(IncomingHttpRequest &request,
			       std::exception_ptr ep,
			       unsigned log_level) noexcept
{
	logger(log_level, ep);
	SendError(request, ep);
}

void
LbHttpConnection::RecordAbuse(double size) noexcept
{
	if (!IsHTTP2())
		return;

	abuse_tarpit.Record(instance.GetEventLoop().SteadyNow(), size);
}

/*
 * http connection handler
 *
 */

void
LbHttpConnection::OnInvalidFrameReceived() noexcept
{
	++instance.http_stats.n_invalid_frames;
	++listener.GetHttpStats().n_invalid_frames;

	RecordAbuse(5);
}

void
LbHttpConnection::RequestHeadersFinished(IncomingHttpRequest &request) noexcept
{
	request.logger = NewFromPool<LbRequestLogger>(request.pool,
						      instance,
						      listener.GetHttpStats(),
						      request);
}

void
LbHttpConnection::ResponseFinished() noexcept
{
	AccountedClientConnection::NoteResponseFinished();
}

void
LbHttpConnection::HandleHttpRequest(IncomingHttpRequest &request,
				    const StopwatchPtr &parent_stopwatch,
				    CancellablePointer &cancel_ptr) noexcept
{
	/* send the HSTS header only on the first response on this
	   connection to save some overhead */
	if (!hsts_sent && listener_config.hsts) {
		request.generate_hsts_header = true;
		hsts_sent = true;
	}

	if (!uri_path_verify_quick(request.uri)) {
		request.body.Clear();
		request.SendMessage(HttpStatus::BAD_REQUEST, "Malformed URI");
		return;
	}

	auto &rl = *(LbRequestLogger *)request.logger;
	if (rl.host == nullptr) {
		request.body.Clear();
		request.SendMessage(HttpStatus::BAD_REQUEST, "No Host header");
		return;
	} else if (!VerifyUriHostPort(rl.host)) {
		request.body.Clear();
		request.SendMessage(HttpStatus::BAD_REQUEST, "Malformed Host header");
		return;
	}

	if (instance.config.global_http_check &&
	    instance.config.global_http_check->Match(request.uri, rl.host) &&
	    instance.config.global_http_check->MatchClientAddress(request.remote_address)) {
		request.body.Clear();

		if (instance.config.global_http_check->Check())
			request.SendMessage(HttpStatus::OK,
					    instance.config.global_http_check->success_message.c_str());
		else
			request.SendSimpleResponse(HttpStatus::NOT_FOUND,
						   nullptr, nullptr);

		return;
	}

	HandleHttpRequest(initial_destination, request, parent_stopwatch, cancel_ptr);
}

void
LbHttpConnection::HandleHttpRequest(const LbGoto &destination,
				    IncomingHttpRequest &request,
				    const StopwatchPtr &parent_stopwatch,
				    CancellablePointer &cancel_ptr) noexcept
{
	const auto &goto_ = destination.FindRequestLeaf(request);
	if (goto_.response != nullptr) {
		request.body.Clear();
		SendResponse(request, *goto_.response);
		return;
	}

	if (goto_.lua != nullptr) {
		InvokeLua(*goto_.lua, request, parent_stopwatch, cancel_ptr);
		return;
	}

	if (goto_.translation != nullptr) {
		AskTranslationServer(*goto_.translation, request, cancel_ptr);
		return;
	}

	if (goto_.handler != nullptr) {
		goto_.handler->HandleHttpRequest(request, parent_stopwatch, cancel_ptr);
		return;
	}

	if (goto_.resolve_connect != nullptr) {
		ResolveConnect(goto_.resolve_connect, request, cancel_ptr);
		return;
	}

	assert(goto_.cluster != nullptr);
	ForwardHttpRequest(*goto_.cluster, request, cancel_ptr);
}

void
LbHttpConnection::ForwardHttpRequest(LbCluster &cluster,
				     IncomingHttpRequest &request,
				     CancellablePointer &cancel_ptr) noexcept
{
	if (!hsts_sent && cluster.GetConfig().hsts) {
		request.generate_hsts_header = true;
		hsts_sent = true;
	}

	if (cluster.GetConfig().tarpit) {
		AccountedClientConnection::NoteRequest();

		if (auto delay = AccountedClientConnection::GetDelay();
		    delay.count() > 0) {
			DelayForwardHttpRequest(*this, request, cluster, delay,
						cancel_ptr);
			return;
		}
	}

	if (IsHTTP2()) {
		if (auto delay = abuse_tarpit.GetDelay(instance.GetEventLoop().SteadyNow());
		    delay.count() > 0) {
			DelayForwardHttpRequest(*this, request, cluster, delay,
						cancel_ptr);
			return;
		}
	}

	::ForwardHttpRequest(*this, request, cluster, cancel_ptr);
}

void
LbHttpConnection::HttpConnectionError(std::exception_ptr e) noexcept
{
	logger(HttpServerLogLevel(e), e);

#ifdef HAVE_NGHTTP2
	assert(http != nullptr || http2);
#endif
	http = nullptr;

	Destroy();
}

void
LbHttpConnection::HttpConnectionClosed() noexcept
{
#ifdef HAVE_NGHTTP2
	assert(http != nullptr || http2);
#endif
	http = nullptr;

	Destroy();
}

std::string
LbHttpConnection::MakeLoggerDomain() const noexcept
{
	return fmt::format("listener='{}' cluster='{}' client='{}'",
			   listener_config.name,
			   listener_config.destination.GetName(),
			   client_address);
}
