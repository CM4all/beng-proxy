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
	MultiWasStock &stock;
	const ChildOptions &options;
	const char *const action;
	const std::span<const char *const> args;
	const unsigned parallelism, concurrency;

public:
	MultiWasRequest(struct pool &_pool, MultiWasStock &_stock,
			StopwatchPtr &&_stopwatch,
			const char *_site_name,
			const ChildOptions &_options,
			const char *_action,
			std::span<const char *const> _args,
			unsigned _parallelism, unsigned _concurrency,
			const char *_remote_host,
			HttpMethod _method, const char *_uri,
			const char *_script_name, const char *_path_info,
			const char *_query_string,
			StringMap &&_headers,
			UnusedIstreamPtr _body,
			std::span<const char *const> _parameters,
			WasMetricsHandler *_metrics_handler,
			::HttpResponseHandler &_handler) noexcept
		:WasStockRequest(_pool, std::move(_stopwatch),
				 _site_name, _remote_host,
				 _method, _uri,
				 _script_name, _path_info, _query_string,
				 std::move(_headers), std::move(_body),
				 _parameters, _metrics_handler, _handler),
		 stock(_stock),
		 options(_options),
		 action(_action), args(_args),
		 parallelism(_parallelism), concurrency(_concurrency) {}

	void Start(CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		GetStockItem();
	}

protected:
	void GetStockItem() noexcept override {
		stock.Get(pool,
			  options,
			  action, args,
			  parallelism, concurrency,
			  *this, cancel_ptr);
	}
};

class RemoteWasRequest final : WasStockRequest
{
	RemoteWasStock &stock;
	const SocketAddress address;
	const unsigned parallelism, concurrency;

public:
	RemoteWasRequest(struct pool &_pool, RemoteWasStock &_stock,
			 StopwatchPtr &&_stopwatch,
			 const char *_site_name,
			 SocketAddress _address,
			 unsigned _parallelism, unsigned _concurrency,
			 const char *_remote_host,
			 HttpMethod _method, const char *_uri,
			 const char *_script_name, const char *_path_info,
			 const char *_query_string,
			 StringMap &&_headers,
			 UnusedIstreamPtr _body,
			 std::span<const char *const> _parameters,
			 WasMetricsHandler *_metrics_handler,
			 ::HttpResponseHandler &_handler) noexcept
		:WasStockRequest(_pool, std::move(_stopwatch),
				 _site_name, _remote_host,
				 _method, _uri,
				 _script_name, _path_info, _query_string,
				 std::move(_headers), std::move(_body),
				 _parameters, _metrics_handler, _handler),
		 stock(_stock),
		 address(_address),
		 parallelism(_parallelism), concurrency(_concurrency) {}

	void Start(CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		GetStockItem();
	}

protected:
	void GetStockItem() noexcept override {
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

	auto request = NewFromPool<MultiWasRequest>(pool, pool, stock,
						    stopwatch_new_was(parent_stopwatch,
								      path, uri,
								      path_info,
								      parameters),
						    site_name,
						    options, action, args,
						    parallelism, concurrency,
						    remote_host,
						    method, uri, script_name,
						    path_info, query_string,
						    std::move(headers),
						    std::move(body),
						    parameters,
						    metrics_handler,
						    handler);
	request->Start(cancel_ptr);
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
	auto request = NewFromPool<RemoteWasRequest>(pool, pool, stock,
						     stopwatch_new_was(parent_stopwatch,
								      address, uri,
								      path_info,
								      parameters),
						     nullptr,
						     address,
						     parallelism, concurrency,
						     remote_host,
						     method, uri, script_name,
						     path_info, query_string,
						     std::move(headers),
						     std::move(body),
						     parameters,
						     metrics_handler,
						     handler);
	request->Start(cancel_ptr);
}
