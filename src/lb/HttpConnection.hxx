/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "http_server/Handler.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "io/Logger.hxx"

#include <boost/intrusive/list_hook.hpp>

template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SslFilter;
class SocketAddress;
struct HttpServerConnection;
namespace NgHttp2 { class ServerConnection; }
struct LbListenerConfig;
class LbCluster;
class LbLuaHandler;
struct LbGoto;
class LbTranslationHandler;
struct LbInstance;

struct LbHttpConnection final
	: PoolHolder, HttpServerConnectionHandler, LoggerDomainFactory,
	  boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

	LbInstance &instance;

	const LbListenerConfig &listener;

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

	LbHttpConnection(PoolPtr &&_pool, LbInstance &_instance,
			 const LbListenerConfig &_listener,
			 const LbGoto &_destination,
			 SocketAddress _client_address);

	void Destroy();
	void CloseAndDestroy();

	using PoolHolder::GetPool;

	bool IsEncrypted() const {
		return ssl_filter != nullptr;
	}

	void SendError(IncomingHttpRequest &request, std::exception_ptr ep);
	void LogSendError(IncomingHttpRequest &request, std::exception_ptr ep,
			  unsigned log_level=2);

	/* virtual methods from class HttpServerConnectionHandler */
	void RequestHeadersFinished(IncomingHttpRequest &request) noexcept override;
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;

	void HttpConnectionError(std::exception_ptr e) noexcept override;
	void HttpConnectionClosed() noexcept override;

public:
	void HandleHttpRequest(const LbGoto &destination,
			       IncomingHttpRequest &request,
			       CancellablePointer &cancel_ptr);

private:
	void ForwardHttpRequest(LbCluster &cluster,
				IncomingHttpRequest &request,
				CancellablePointer &cancel_ptr);

	void InvokeLua(LbLuaHandler &handler,
		       IncomingHttpRequest &request,
		       CancellablePointer &cancel_ptr);

	void AskTranslationServer(LbTranslationHandler &handler,
				  IncomingHttpRequest &request,
				  CancellablePointer &cancel_ptr);

	void ResolveConnect(const char *host,
			    IncomingHttpRequest &request,
			    CancellablePointer &cancel_ptr);

protected:
	/* virtual methods from class LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override;
};

LbHttpConnection *
NewLbHttpConnection(LbInstance &instance,
		    const LbListenerConfig &listener,
		    const LbGoto &destination,
		    PoolPtr pool,
		    UniquePoolPtr<FilteredSocket> socket,
		    const SslFilter *ssl_filter,
		    SocketAddress address);
