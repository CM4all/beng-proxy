// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/server/Handler.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "io/Logger.hxx"
#include "util/IntrusiveList.hxx"

template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SslFilter;
struct BpConfig;
struct BpInstance;
class BpListener;
class SocketAddress;
struct HttpServerConnection;
namespace NgHttp2 { class ServerConnection; }

/*
 * A connection from a HTTP client.
 */
struct BpConnection final
	: PoolHolder, HttpServerConnectionHandler, HttpServerRequestHandler,
	  public IntrusiveListHook<IntrusiveHookMode::NORMAL>
{
	BpInstance &instance;
	BpListener &listener;
	const BpConfig &config;

	/**
	 * The address (host and port) of the client.
	 */
	const char *const remote_host_and_port;

	const LLogger logger;

	const char *const peer_subject, *const peer_issuer_subject;

	HttpServerRequestHandler *request_handler = nullptr;

	HttpServerConnection *http = nullptr;

#ifdef HAVE_NGHTTP2
	UniquePoolPtr<NgHttp2::ServerConnection> http2;
#endif

	const bool ssl;

	BpConnection(PoolPtr &&_pool, BpInstance &_instance,
		     BpListener &_listener,
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

BpConnection *
new_connection(PoolPtr pool, BpInstance &instance,
	       BpListener &listener,
	       HttpServerRequestHandler *request_handler,
	       UniquePoolPtr<FilteredSocket> socket,
	       const SslFilter *ssl_filter,
	       SocketAddress address) noexcept;
