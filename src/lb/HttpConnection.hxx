/*
 * Copyright 2007-2019 CM4all GmbH
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
#include "io/Logger.hxx"

#include <boost/intrusive/list.hpp>

#include <chrono>

#include <stdint.h>

struct SslFactory;
struct SslFilter;
class UniqueSocketDescriptor;
class SocketAddress;
struct HttpServerConnection;
struct LbListenerConfig;
class LbCluster;
class LbLuaHandler;
class LbTranslationHandler;
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

	SslFilter *ssl_filter = nullptr;

	HttpServerConnection *http;

	/**
	 * Attributes which are specific to the current request.  They are
	 * only valid while a request is being handled (i.e. during the
	 * lifetime of the #IncomingHttpRequest instance).  Strings are
	 * allocated from the request pool.
	 *
	 * The request header pointers are here because our
	 * http_client_request() call invalidates the original request
	 * header StringMap instance, but after that, the access logger
	 * needs these header values.
	 */
	struct PerRequest {
		/**
		 * The time stamp at the start of the request.  Used to calculate
		 * the request duration.
		 */
		std::chrono::steady_clock::time_point start_time;

		/**
		 * The "Host" request header.
		 */
		const char *host;

		/**
		 * The "X-Forwarded-For" request header.
		 */
		const char *x_forwarded_for;

		/**
		 * The "Referer" [sic] request header.
		 */
		const char *referer;

		/**
		 * The "User-Agent" request header.
		 */
		const char *user_agent;

		/**
		 * The current request's canonical host name (from
		 * #TRANSLATE_CANONICAL_HOST).
		 */
		const char *canonical_host;

		/**
		 * The name of the site being accessed by the current HTTP
		 * request (from #TRANSLATE_SITE).  It is a hack to allow the
		 * "log" callback to see this information.
		 */
		const char *site_name;

		/**
		 * @see LOG_FORWARDED_TO
		 */
		const char *forwarded_to;

		void Begin(const IncomingHttpRequest &request,
			   std::chrono::steady_clock::time_point now);

		constexpr const char *GetCanonicalHost() const {
			return canonical_host != nullptr
				? canonical_host
				: host;
		}

		std::chrono::steady_clock::duration GetDuration(std::chrono::steady_clock::time_point now) const {
			return now - start_time;
		}
	} per_request;

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
	void LogSendError(IncomingHttpRequest &request, std::exception_ptr ep);

	/* virtual methods from class HttpServerConnectionHandler */
	void RequestHeadersFinished(const IncomingHttpRequest &request) noexcept override;
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;

	void LogHttpRequest(IncomingHttpRequest &request,
			    http_status_t status, off_t length,
			    uint64_t bytes_received,
			    uint64_t bytes_sent) noexcept override;

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
		    SslFactory *ssl_factory,
		    UniqueSocketDescriptor &&fd, SocketAddress address);
