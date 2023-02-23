// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <span>

enum class HttpMethod : uint_least8_t;
struct pool;
class StopwatchPtr;
class UnusedIstreamPtr;
class WasStock;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
struct ChildOptions;

/**
 * High level WAS client.
 *
 * @param args command-line arguments
 */
void
was_request(struct pool &pool, WasStock &was_stock,
	    const StopwatchPtr &parent_stopwatch,
	    const char *site_name,
	    const ChildOptions &options,
	    const char *action,
	    const char *path,
	    std::span<const char *const> args,
	    unsigned parallelism, bool disposable,
	    const char *remote_host,
	    HttpMethod method, const char *uri,
	    const char *script_name, const char *path_info,
	    const char *query_string,
	    StringMap &&headers, UnusedIstreamPtr body,
	    std::span<const char *const> params,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr);
