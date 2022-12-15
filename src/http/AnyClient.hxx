/*
 * Copyright 2007-2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
class SocketFilterFactory;
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
			SocketFilterFactory &filter_factory,
			HttpMethod method,
			const HttpAddress &address,
			StringMap &&headers, UnusedIstreamPtr body,
			HttpResponseHandler &handler,
			CancellablePointer &cancel_ptr) noexcept;
};
