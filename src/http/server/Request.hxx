// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stopwatch.hxx"
#include "http/IncomingRequest.hxx"

struct HttpServerConnection;

struct HttpServerRequest final : public IncomingHttpRequest {
	HttpServerConnection &connection;

	RootStopwatchPtr stopwatch;

	HttpServerRequest(PoolPtr &&_pool, HttpServerConnection &_connection,
			  SocketAddress _local_address,
			  SocketAddress _remote_address,
			  const char *_local_host_and_port,
			  const char *_remote_host,
			  HttpMethod _method,
			  std::string_view _uri) noexcept;

	void Destroy() noexcept;

	/* virtual methods from class IncomingHttpRequest */
	void SendResponse(HttpStatus status,
			  HttpHeaders &&response_headers,
			  UnusedIstreamPtr response_body) noexcept override;
};
