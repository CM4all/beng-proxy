// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "MGlue.hxx"
#include "MStock.hxx"
#include "RStock.hxx"
#include "SRequest.hxx"
#include "was/async/Socket.hxx"
#include "pool/pool.hxx"
#include "stopwatch.hxx"
#include "net/SocketAddress.hxx"
#include "net/FormatAddress.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"

#include <cassert>

class MultiWasRequest final : WasStockRequest
{
public:
	using WasStockRequest::WasStockRequest;

	void Start(MultiWasStock &stock, const ChildOptions &options,
		   const char *action, std::span<const char *const> args,
		   unsigned parallelism, unsigned concurrency,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		stock.Get(pool,
			  options,
			  action, args,
			  parallelism, concurrency,
			  *this, cancel_ptr);
	}

	void Start(RemoteWasStock &stock, SocketAddress address,
		   unsigned parallelism, unsigned concurrency,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		stock.Get(pool, address, parallelism, concurrency,
			  *this, cancel_ptr);
	}
};

/*
 * constructor
 *
 */

#ifdef ENABLE_STOPWATCH

[[gnu::pure]]
static const char *
GetComaClass(std::span<const char *const> parameters)
{
	for (const char *i : parameters) {
		const char *result = StringAfterPrefix(i, "COMA_CLASS=");
		if (result != nullptr && *result != 0)
			return result;
	}

	return nullptr;
}

#endif

static StopwatchPtr
stopwatch_new_was(const StopwatchPtr &parent_stopwatch,
		  const char *path, const char *uri,
		  const char *path_info,
		  std::span<const char *const> parameters)
{
#ifdef ENABLE_STOPWATCH
	assert(path != nullptr);
	assert(uri != nullptr);

	if (!stopwatch_is_enabled())
		return nullptr;

	/* special case for a very common COMA application */
	const char *coma_class = GetComaClass(parameters);
	if (coma_class != nullptr)
		path = coma_class;

	const char *slash = strrchr(path, '/');
	if (slash != nullptr && slash[1] != 0)
		path = slash + 1;

	if (path_info != nullptr && *path_info != 0)
		uri = path_info;

	std::string name = path;
	name.push_back(' ');
	name += uri;

	return StopwatchPtr(parent_stopwatch, name.c_str());
#else
	(void)parent_stopwatch;
	(void)path;
	(void)uri;
	(void)path_info;
	(void)parameters;
	return nullptr;
#endif
}

void
SendMultiWasRequest(struct pool &pool, MultiWasStock &stock,
		    const StopwatchPtr &parent_stopwatch,
		    const char *site_name,
		    const ChildOptions &options,
		    const char *action,
		    const char *path,
		    std::span<const char *const> args,
		    unsigned parallelism,
		    const char *remote_host,
		    HttpMethod method, const char *uri,
		    const char *script_name, const char *path_info,
		    const char *query_string,
		    StringMap &&headers, UnusedIstreamPtr body,
		    std::span<const char *const> parameters,
		    unsigned concurrency,
		    WasMetricsHandler *metrics_handler,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept
{
	if (action == nullptr)
		action = path;

	auto request = NewFromPool<MultiWasRequest>(pool, pool,
						    stopwatch_new_was(parent_stopwatch,
								      path, uri,
								      path_info,
								      parameters),
						    site_name,
						    remote_host,
						    method, uri, script_name,
						    path_info, query_string,
						    std::move(headers),
						    std::move(body),
						    parameters,
						    metrics_handler,
						    handler);
	request->Start(stock, options, action, args, parallelism, concurrency, cancel_ptr);
}

static StopwatchPtr
stopwatch_new_was(const StopwatchPtr &parent_stopwatch,
		  SocketAddress address, const char *uri,
		  const char *path_info,
		  std::span<const char *const> parameters)
{
#ifdef ENABLE_STOPWATCH
	assert(!address.IsNull());
	assert(address.IsDefined());
	assert(uri != nullptr);

	if (!stopwatch_is_enabled())
		return nullptr;

	const char *path = nullptr;

	/* special case for a very common COMA application */
	const char *coma_class = GetComaClass(parameters);
	if (coma_class != nullptr)
		path = coma_class;

	char path_buffer[1024];
	if (path == nullptr) {
		if (!ToString(path_buffer, address))
			return nullptr;
		path = path_buffer;
	}

	const char *slash = strrchr(path, '/');
	if (slash != nullptr && slash[1] != 0)
		path = slash + 1;

	if (path_info != nullptr && *path_info != 0)
		uri = path_info;

	std::string name = path;
	name.push_back(' ');
	name += uri;

	return StopwatchPtr(parent_stopwatch, name.c_str());
#else
	(void)parent_stopwatch;
	(void)address;
	(void)uri;
	(void)path_info;
	(void)parameters;
	return nullptr;
#endif
}

void
SendRemoteWasRequest(struct pool &pool, RemoteWasStock &stock,
		     const StopwatchPtr &parent_stopwatch,
		     SocketAddress address,
		     unsigned parallelism,
		     const char *remote_host,
		     HttpMethod method, const char *uri,
		     const char *script_name, const char *path_info,
		     const char *query_string,
		     StringMap &&headers, UnusedIstreamPtr body,
		     std::span<const char *const> parameters,
		     unsigned concurrency,
		     WasMetricsHandler *metrics_handler,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept
{
	auto request = NewFromPool<MultiWasRequest>(pool, pool,
						    stopwatch_new_was(parent_stopwatch,
								      address, uri,
								      path_info,
								      parameters),
						    nullptr,
						    remote_host,
						    method, uri, script_name,
						    path_info, query_string,
						    std::move(headers),
						    std::move(body),
						    parameters,
						    metrics_handler,
						    handler);
	request->Start(stock, address, parallelism, concurrency, cancel_ptr);
}
