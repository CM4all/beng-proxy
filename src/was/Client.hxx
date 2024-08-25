// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <exception>
#include <span>

enum class HttpMethod : uint_least8_t;
struct pool;
class StopwatchPtr;
class FileDescriptor;
class SocketDescriptor;
class EventLoop;
class UnusedIstreamPtr;
class WasLease;
class StringMap;
class WasMetricsHandler;
class HttpResponseHandler;
class CancellablePointer;

/**
 * Is it worth retrying after this error?
 */
[[gnu::pure]]
bool
IsWasClientRetryFailure(std::exception_ptr error) noexcept;

/**
 * Web Application Socket client.
 *
 * Sends a HTTP request on a socket to a WAS server, and passes the
 * response to the handler.
 *
 * @param pool the memory pool; this client holds a reference until
 * the response callback has returned and the response body is closed
 * @param control_fd a control socket to the WAS server
 * @param input_fd a data pipe for the response body
 * @param output_fd a data pipe for the request body
 * @param lease the lease for both sockets
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param script_name the URI part of the script
 * @param path_info the URI part following the script name
 * @param query_string the query string (without the question mark)
 * @param headers the request headers (optional)
 * @param body the request body (optional)
 * @param params application specific parameters
 * @param metrics_handler if not nullptr, then enable metrix, to be
 * delivered to this handler
 * @param handler receives the response
 * @param cancel_ptr a handle which may be used to abort the operation
 */
void
was_client_request(struct pool &pool, EventLoop &event_loop,
		   StopwatchPtr stopwatch,
		   SocketDescriptor control_fd,
		   FileDescriptor input_fd, FileDescriptor output_fd,
		   WasLease &lease,
		   const char *remote_host,
		   HttpMethod method, const char *uri,
		   const char *script_name, const char *path_info,
		   const char *query_string,
		   const StringMap &headers, UnusedIstreamPtr body,
		   std::span<const char *const> params,
		   WasMetricsHandler *metrics_handler,
		   HttpResponseHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;

