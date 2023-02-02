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

#include "http/server/Handler.hxx"
#include "net/ClientAccounting.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "io/Logger.hxx"
#include "util/IntrusiveList.hxx"

template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SslFilter;
class SocketAddress;
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

	/**
	 * The client's address formatted as a string (for logging).  This
	 * is guaranteed to be non-nullptr.
	 */
	const char *client_address;

	const LazyDomainLogger logger;

	const SslFilter *ssl_filter = nullptr;

	HttpServerConnection *http = nullptr;

#ifdef HAVE_NGHTTP2
	UniquePoolPtr<NgHttp2::ServerConnection> http2;
#endif

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

	void SendError(IncomingHttpRequest &request, std::exception_ptr ep) noexcept;
	void LogSendError(IncomingHttpRequest &request, std::exception_ptr ep,
			  unsigned log_level=2) noexcept;

	/* virtual methods from class HttpServerConnectionHandler */
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
