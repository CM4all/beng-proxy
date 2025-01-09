// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * HTTP client implementation.
 */

#pragma once

#include <cstdint>
#include <stdexcept>

enum class HttpMethod : uint_least8_t;
struct pool;
class StopwatchPtr;
class UnusedIstreamPtr;
class FilteredSocket;
class Lease;
class HttpResponseHandler;
class CancellablePointer;
class StringMap;
class GrowingBuffer;

/**
 * Error codes for #HttpClientError.
 */
enum class HttpClientErrorCode {
	UNSPECIFIED,

	/**
	 * The server has closed the connection before the first response
	 * byte.
	 */
	REFUSED,

	/**
	 * A socket I/O error has occurred.
	 */
	IO,

	/**
	 * Non-HTTP garbage was received.
	 */
	GARBAGE,
};

class HttpClientError : public std::runtime_error {
	HttpClientErrorCode code;

public:
	HttpClientError(HttpClientErrorCode _code, const char *_msg)
		:std::runtime_error(_msg), code(_code) {}

	HttpClientErrorCode GetCode() const {
		return code;
	}
};

/**
 * Is the specified error a server failure, that justifies
 * blacklisting the server for a while?
 */
[[gnu::pure]]
bool
IsHttpClientServerFailure(std::exception_ptr ep) noexcept;

/**
 * Is it worth retrying after this error?
 */
bool
IsHttpClientRetryFailure(std::exception_ptr ep) noexcept;

/**
 * Sends a HTTP request on a socket, and passes the response to the
 * handler.
 *
 * @param pool the memory pool; this client holds a reference until
 * the response callback has returned and the response body is closed
 * @param socket a socket to the HTTP server
 * @param lease the lease for the socket
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param headers the serialized request headers (optional)
 * @param body the request body (optional)
 * @param expect_100 true to send "Expect: 100-continue" in the
 * presence of a request body
 * @param handler receives the response
 * @param async_ref a handle which may be used to abort the operation
 */
void
http_client_request(struct pool &pool,
		    StopwatchPtr stopwatch,
		    FilteredSocket &socket, Lease &lease,
		    const char *peer_name,
		    HttpMethod method, const char *uri,
		    const StringMap &headers,
		    GrowingBuffer &&more_headers,
		    UnusedIstreamPtr body, bool expect_100,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept;
