// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Score.hxx"

class SlicePool;
struct pool;
template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SocketAddress;
class SocketDescriptor;

struct HttpServerConnection;
class HttpServerConnectionHandler;
class HttpServerRequestHandler;

/**
 * @param date_header generate Date response headers?
 */
HttpServerConnection *
http_server_connection_new(struct pool &pool,
			   UniquePoolPtr<FilteredSocket> socket,
			   SocketAddress local_address,
			   SocketAddress remote_address,
			   bool date_header,
			   SlicePool &request_slice_pool,
			   HttpServerConnectionHandler &handler,
			   HttpServerRequestHandler &request_handler) noexcept;

void
http_server_connection_close(HttpServerConnection *connection) noexcept;

[[gnu::pure]]
SocketDescriptor
http_server_connection_get_socket(const HttpServerConnection &connection) noexcept;

void
http_server_connection_graceful(HttpServerConnection *connection) noexcept;

HttpServerScore
http_server_connection_score(const HttpServerConnection *connection) noexcept;
