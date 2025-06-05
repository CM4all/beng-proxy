// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "strmap.hxx"
#include "net/SocketAddress.hxx"
#include "pool/Ptr.hxx"
#include "istream/UnusedPtr.hxx"

#include <cstdint>
#include <string_view>

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;
struct pool;
class StringMap;
class HttpHeaders;
class IncomingHttpRequestLogger;

struct IncomingHttpRequest {
	const PoolPtr pool;

	const SocketAddress local_address, remote_address;

	/**
	 * The local address (host and port) that was connected to.
	 */
	const char *const local_host_and_port;

	/**
	 * The address of the client, without the port number.
	 */
	const char *const remote_host;

	/* request metadata */
	HttpMethod method;
	const char *uri;
	StringMap headers;

	/**
	 * The request body.  The handler is responsible for closing this
	 * istream.
	 */
	UnusedIstreamPtr body;

	IncomingHttpRequestLogger *logger = nullptr;

	/**
	 * If true, then the response will have a
	 * "Strict-Transport-Security" header.
	 */
	bool generate_hsts_header = false;

protected:
	IncomingHttpRequest(PoolPtr &&_pool,
			    SocketAddress _local_address,
			    SocketAddress _remote_address,
			    const char *_local_host_and_port,
			    const char *_remote_host) noexcept;

	IncomingHttpRequest(PoolPtr &&_pool,
			    SocketAddress _local_address,
			    SocketAddress _remote_address,
			    const char *_local_host_and_port,
			    const char *_remote_host,
			    HttpMethod _method,
			    std::string_view _uri) noexcept;

	~IncomingHttpRequest() noexcept;

	IncomingHttpRequest(const IncomingHttpRequest &) = delete;
	IncomingHttpRequest &operator=(const IncomingHttpRequest &) = delete;

public:
	bool HasBody() const noexcept {
		return body;
	}

	virtual void SendResponse(HttpStatus status,
				  HttpHeaders &&response_headers,
				  UnusedIstreamPtr response_body) noexcept = 0;

	/**
	 * Generate a "simple" response with an optional plain-text body and
	 * an optional "Location" redirect header.
	 */
	void SendSimpleResponse(HttpStatus status, std::string_view location,
				std::string_view msg) noexcept;

	void SendMessage(HttpStatus status, std::string_view msg) noexcept;

	void SendRedirect(HttpStatus status, std::string_view location,
			  std::string_view msg) noexcept;
};
