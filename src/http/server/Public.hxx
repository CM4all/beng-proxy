// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class SlicePool;
struct pool;
template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SocketAddress;

struct HttpServerConnection;
class HttpServerConnectionHandler;
class HttpServerRequestHandler;

/**
 * The score of a connection.  This is used under high load to
 * estimate which connections should be dropped first, as a remedy for
 * denial of service attacks.
 */
enum http_server_score {
	/**
	 * Connection has been accepted, but client hasn't sent any data
	 * yet.
	 */
	HTTP_SERVER_NEW,

	/**
	 * Client is transmitting the very first request.
	 */
	HTTP_SERVER_FIRST,

	/**
	 * At least one request was completed, but none was successful.
	 */
	HTTP_SERVER_ERROR,

	/**
	 * At least one request was completed successfully.
	 */
	HTTP_SERVER_SUCCESS,
};

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

void
http_server_connection_graceful(HttpServerConnection *connection) noexcept;

enum http_server_score
http_server_connection_score(const HttpServerConnection *connection) noexcept;
