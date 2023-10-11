// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "AbuseTarpit.hxx"
#include "http/server/Handler.hxx"
#include "net/ClientAccounting.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "net/StaticSocketAddress.hxx"
#include "io/Logger.hxx"
#include "util/IntrusiveList.hxx"

template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SslFilter;
struct HttpServerConnection;
namespace NgHttp2 { class ServerConnection; }
class LbListener;
struct LbListenerConfig;
class LbCluster;
class LbLuaHandler;
struct LbGoto;
class LbTranslationHandler;
struct LbInstance;
class StringMap;
class AllocatorPtr;

struct LbHttpConnection final
	: PoolHolder, HttpServerConnectionHandler, HttpServerRequestHandler,
	  AccountedClientConnection,
	  LoggerDomainFactory,
	  public IntrusiveListHook<IntrusiveHookMode::NORMAL>
{
	LbInstance &instance;

	LbListener &listener;
	const LbListenerConfig &listener_config;

	const LbGoto &initial_destination;

	const StaticSocketAddress client_address;

	const LazyDomainLogger logger;

	const SslFilter *ssl_filter = nullptr;

	HttpServerConnection *http = nullptr;

#ifdef HAVE_NGHTTP2
	UniquePoolPtr<NgHttp2::ServerConnection> http2;
#endif

	AbuseTarpit abuse_tarpit;

	bool hsts;

	LbHttpConnection(PoolPtr &&_pool, LbInstance &_instance,
			 LbListener &_listener,
			 const LbGoto &_destination,
			 SocketAddress _client_address) noexcept;

	void Destroy() noexcept;
	void CloseAndDestroy() noexcept;

	using PoolHolder::GetPool;

	bool IsEncrypted() const noexcept {
		return ssl_filter != nullptr;
	}

	bool IsHTTP2() const noexcept {
#ifdef HAVE_NGHTTP2
		return http2;
#else
		return false;
#endif
	}

	/**
	 * Call when a request was canceled (e.g. via HTTP/2
	 * RST_STREAM).  This keeps track of #abuse_tarpit.
	 *
	 * @param the token bucket "size" parameter; defaults to 1
	 * (one request was canceled), but higher scores can be used
	 * for more severe events
	 */
	void RecordAbuse(double size=1) noexcept;

	void SendError(IncomingHttpRequest &request, std::exception_ptr ep) noexcept;
	void LogSendError(IncomingHttpRequest &request, std::exception_ptr ep,
			  unsigned log_level=2) noexcept;

	/* virtual methods from class HttpServerConnectionHandler */
	void OnInvalidFrameReceived() noexcept override;
	void RequestHeadersFinished(IncomingHttpRequest &request) noexcept override;
	void ResponseFinished() noexcept override;
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;

	void HttpConnectionError(std::exception_ptr e) noexcept override;
	void HttpConnectionClosed() noexcept override;

public:
	void HandleHttpRequest(const LbGoto &destination,
			       IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept;

private:
	void ForwardHttpRequest(LbCluster &cluster,
				IncomingHttpRequest &request,
				CancellablePointer &cancel_ptr) noexcept;

	void InvokeLua(LbLuaHandler &handler,
		       IncomingHttpRequest &request,
		       const StopwatchPtr &parent_stopwatch,
		       CancellablePointer &cancel_ptr) noexcept;

	void AskTranslationServer(LbTranslationHandler &handler,
				  IncomingHttpRequest &request,
				  CancellablePointer &cancel_ptr) noexcept;

	void ResolveConnect(const char *host,
			    IncomingHttpRequest &request,
			    CancellablePointer &cancel_ptr) noexcept;

protected:
	/* virtual methods from class LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override;
};

LbHttpConnection *
NewLbHttpConnection(LbInstance &instance,
		    LbListener &listener,
		    const LbGoto &destination,
		    PoolPtr pool,
		    UniquePoolPtr<FilteredSocket> socket,
		    const SslFilter *ssl_filter,
		    SocketAddress address) noexcept;
