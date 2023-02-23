// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <span>

enum class HttpMethod : uint_least8_t;
struct pool;
class EventLoop;
class UnusedIstreamPtr;
class FcgiStock;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
class UniqueFileDescriptor;
class StopwatchPtr;
struct ChildOptions;

/**
 * High level FastCGI client.
 *
 * @param args command-line arguments
 */
void
fcgi_request(struct pool *pool, EventLoop &event_loop,
	     FcgiStock *fcgi_stock,
	     const StopwatchPtr &parent_stopwatch,
	     const char *site_name,
	     const ChildOptions &options,
	     const char *action,
	     const char *path,
	     std::span<const char *const> args,
	     unsigned parallelism,
	     HttpMethod method, const char *uri,
	     const char *script_name, const char *path_info,
	     const char *query_string,
	     const char *document_root,
	     const char *remote_addr,
	     StringMap &&headers, UnusedIstreamPtr body,
	     std::span<const char *const> params,
	     UniqueFileDescriptor &&stderr_fd,
	     HttpResponseHandler &handler,
	     CancellablePointer &cancel_ptr) noexcept;
