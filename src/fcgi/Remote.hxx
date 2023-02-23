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
class TcpBalancer;
struct AddressList;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
class UniqueFileDescriptor;
class StopwatchPtr;

/**
 * High level FastCGI client for remote FastCGI servers.
 */
void
fcgi_remote_request(struct pool *pool, EventLoop &event_loop,
		    TcpBalancer *tcp_balancer,
		    const StopwatchPtr &parent_stopwatch,
		    const AddressList *address_list,
		    const char *path,
		    HttpMethod method, const char *uri,
		    const char *script_name, const char *path_info,
		    const char *query_string,
		    const char *document_root,
		    const char *remote_addr,
		    StringMap &&headers, UnusedIstreamPtr body,
		    std::span<const char *const> params,
		    UniqueFileDescriptor stderr_fd,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr);
