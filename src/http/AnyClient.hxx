// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "cluster/StickyHash.hxx"

#include <cstdint>

#ifdef HAVE_NGHTTP2
#include <string>
#include <map>
#endif

enum class HttpMethod : uint_least8_t;
struct pool;
class EventLoop;
class StopwatchPtr;
class UnusedIstreamPtr;
class SslClientFactory;
class FilteredSocketBalancer;
namespace NgHttp2 { class Stock; }
class SocketFilterParams;
struct HttpAddress;
class HttpResponseHandler;
class CancellablePointer;
class StringMap;

/**
 * Invoke either an HTTP/2 or an HTTP/1.2 client.
 */
class AnyHttpClient {
	FilteredSocketBalancer &fs_balancer;

#ifdef HAVE_NGHTTP2
	NgHttp2::Stock &nghttp2_stock;

	struct Request;
	struct Waiting;
	class Probe;
	std::map<std::string, Probe> probes;
#endif

	SslClientFactory *const ssl_client_factory;

public:
	AnyHttpClient(FilteredSocketBalancer &_fs_balancer,
#ifdef HAVE_NGHTTP2
		      NgHttp2::Stock &_nghttp2_stock,
#endif
		      SslClientFactory *_ssl_client_factory) noexcept;

	~AnyHttpClient() noexcept;

	EventLoop &GetEventLoop() const noexcept;

	/**
	 * Throws on error.
	 *
	 * @param sticky_hash a portion of the session id that is used to
	 * select the worker; 0 means disable stickiness
	 */
	void SendRequest(struct pool &pool,
			 const StopwatchPtr &parent_stopwatch,
			 sticky_hash_t sticky_hash,
			 HttpMethod method,
			 const HttpAddress &address,
			 StringMap &&headers, UnusedIstreamPtr body,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr);

private:
	void ProbeHTTP2(struct pool &pool,
			const StopwatchPtr &parent_stopwatch,
			sticky_hash_t sticky_hash,
			const SocketFilterParams &filter_params,
			HttpMethod method,
			const HttpAddress &address,
			StringMap &&headers, UnusedIstreamPtr body,
			HttpResponseHandler &handler,
			CancellablePointer &cancel_ptr) noexcept;
};
