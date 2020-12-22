/*
 * Copyright 2007-2020 CM4all GmbH
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
struct BpConfig;
struct BpInstance;
class BPListener;
class SocketAddress;
struct HttpServerConnection;
namespace NgHttp2 { class ServerConnection; }

/*
 * A connection from a HTTP client.
 */
struct BpConnection final
	: PoolHolder, HttpServerConnectionHandler,
	  boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

	BpInstance &instance;
	BPListener &listener;
	const BpConfig &config;

	/**
	 * The address (host and port) of the client.
	 */
	const char *const remote_host_and_port;

	const LLogger logger;

	const char *const peer_subject, *const peer_issuer_subject;

	HttpServerConnection *http = nullptr;

#ifdef HAVE_NGHTTP2
	UniquePoolPtr<NgHttp2::ServerConnection> http2;
#endif

	BpConnection(PoolPtr &&_pool, BpInstance &_instance,
		     BPListener &_listener,
		     SocketAddress remote_address,
		     const SslFilter *_ssl_filter) noexcept;
	~BpConnection() noexcept;

	using PoolHolder::GetPool;

	struct Disposer {
		void operator()(BpConnection *c) noexcept;
	};

	/* virtual methods from class HttpServerConnectionHandler */
	void RequestHeadersFinished(IncomingHttpRequest &request) noexcept override;
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;

	void HttpConnectionError(std::exception_ptr e) noexcept override;
	void HttpConnectionClosed() noexcept override;
};

void
new_connection(PoolPtr pool, BpInstance &instance,
	       BPListener &listener,
	       UniquePoolPtr<FilteredSocket> socket,
	       const SslFilter *ssl_filter,
	       SocketAddress address) noexcept;

void
close_connection(BpConnection *connection) noexcept;
