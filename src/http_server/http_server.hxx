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

struct pool;
template<typename T> class UniquePoolPtr;
class FilteredSocket;
class SocketAddress;

struct HttpServerConnection;
class HttpServerConnectionHandler;

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
			   HttpServerConnectionHandler &handler) noexcept;

void
http_server_connection_close(HttpServerConnection *connection) noexcept;

void
http_server_connection_graceful(HttpServerConnection *connection) noexcept;

enum http_server_score
http_server_connection_score(const HttpServerConnection *connection) noexcept;
