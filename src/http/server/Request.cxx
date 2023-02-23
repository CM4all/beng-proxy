// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * HTTP server implementation.
 */

#include "Request.hxx"
#include "pool/pool.hxx"

HttpServerRequest::HttpServerRequest(PoolPtr &&_pool,
				     HttpServerConnection &_connection,
				     SocketAddress _local_address,
				     SocketAddress _remote_address,
				     const char *_local_host_and_port,
				     const char *_remote_host,
				     HttpMethod _method,
				     std::string_view _uri) noexcept
	:IncomingHttpRequest(std::move(_pool),
			     _local_address, _remote_address,
			     _local_host_and_port, _remote_host,
			     _method, _uri),
	 connection(_connection),
	 stopwatch(uri) {}

void
HttpServerRequest::Destroy() noexcept
{
	pool_trash(pool);
	this->~HttpServerRequest();
}
