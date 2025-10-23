// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <exception>
#include <span>

enum class HttpMethod : uint_least8_t;
struct pool;
class UnusedIstreamPtr;
class BufferedSocket;
class Lease;
class UniqueFileDescriptor;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
class StopwatchPtr;

/**
 * Is it worth retrying after this error?
 */
[[gnu::pure]]
bool
IsFcgiClientRetryFailure(const std::exception_ptr &error) noexcept;

/**
 * Sends a HTTP request on a socket to an FastCGI server, and passes
 * the response to the handler.
 *
 * @param pool the memory pool; this client holds a reference until
 * the response callback has returned and the response body is closed
 * @param fd a socket to the HTTP server
 * @param fd_type the exact socket type
 * @param lease the lease for the socket
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param script_filename the absolue path name of the script
 * @param script_name the URI part of the script
 * @param path_info the URI part following the script name
 * @param query_string the query string (without the question mark)
 * @param document_root the absolute path of the document root
 * @param headers the serialized request headers (optional)
 * @param body the request body (optional)
 * @param stderr_fd a file descriptor for #FCGI_STDERR packets (will
 * be closed by this library) or -1 to send everything to stderr
 * @param handler receives the response
 * @param async_ref a handle which may be used to abort the operation
 */
void
fcgi_client_request(struct pool *pool,
		    StopwatchPtr stopwatch,
		    BufferedSocket &socket, Lease &lease,
		    HttpMethod method, const char *uri,
		    const char *script_filename,
		    const char *script_name, const char *path_info,
		    const char *query_string,
		    const char *document_root,
		    const char *remote_addr,
		    const StringMap &headers, UnusedIstreamPtr body,
		    std::span<const char *const> params,
		    UniqueFileDescriptor &&stderr_fd,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept;
