/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "AnyClient.hxx"
#include "Address.hxx"
#include "GlueClient.hxx"
#include "pool/pool.hxx"
#include "istream/UnusedPtr.hxx"
#include "nghttp2/Glue.hxx"
#include "ssl/SslSocketFilterFactory.hxx"
#include "fs/Balancer.hxx"
#include "net/HostParser.hxx"

AnyHttpClient::AnyHttpClient(FilteredSocketBalancer &_fs_balancer,
#ifdef HAVE_NGHTTP2
			     NgHttp2::Stock &_nghttp2_stock,
#endif
			     SslClientFactory *_ssl_client_factory) noexcept
	:fs_balancer(_fs_balancer),
#ifdef HAVE_NGHTTP2
	 nghttp2_stock(_nghttp2_stock),
#endif
	 ssl_client_factory(_ssl_client_factory)
{
}

AnyHttpClient::~AnyHttpClient() noexcept = default;

inline EventLoop &
AnyHttpClient::GetEventLoop() const noexcept
{
	return fs_balancer.GetEventLoop();
}

[[gnu::pure]]
static const char *
GetHostWithoutPort(struct pool &pool, const HttpAddress &address) noexcept
{
	const char *host_and_port = address.host_and_port;
	if (host_and_port == nullptr)
		return nullptr;

	auto e = ExtractHost(host_and_port);
	if (e.host.IsNull())
		return nullptr;

	return p_strdup(pool, e.host);
}

void
AnyHttpClient::SendRequest(struct pool &pool,
			   const StopwatchPtr &parent_stopwatch,
			   sticky_hash_t sticky_hash,
			   http_method_t method,
			   const HttpAddress &address,
			   StringMap &&headers, UnusedIstreamPtr body,
			   HttpResponseHandler &handler,
			   CancellablePointer &cancel_ptr)
{
	auto &event_loop = GetEventLoop();

	SocketFilterFactory *filter_factory = nullptr;

	if (address.ssl) {
		if (ssl_client_factory == nullptr)
			throw std::runtime_error("SSL support is disabled");

		auto alpn = address.http2
			? SslClientAlpn::HTTP_2
			: SslClientAlpn::NONE;

		filter_factory = NewFromPool<SslSocketFilterFactory>(pool,
								     event_loop,
								     *ssl_client_factory,
								     GetHostWithoutPort(pool, address),
								     address.certificate,
								     alpn);
	}

#ifdef HAVE_NGHTTP2
	if (address.http2)
		NgHttp2::SendRequest(pool, event_loop, nghttp2_stock,
				     parent_stopwatch,
				     filter_factory,
				     method, address,
				     std::move(headers),
				     std::move(body),
				     nullptr,
				     handler, cancel_ptr);
	else
#endif
		http_request(pool, event_loop, fs_balancer,
			     parent_stopwatch,
			     sticky_hash,
			     filter_factory,
			     method, address,
			     std::move(headers),
			     std::move(body),
			     handler, cancel_ptr);
}
