/*
 * Copyright 2007-2017 Content Management AG
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

#ifndef BENG_PROXY_DIRECT_RESOURCE_LOADER_HXX
#define BENG_PROXY_DIRECT_RESOURCE_LOADER_HXX

#include "ResourceLoader.hxx"

class EventLoop;
class SpawnService;
class WasStock;
class StockMap;
class LhttpStock;
class FcgiStock;
class NfsCache;
class TcpBalancer;
namespace Uring { class Queue; }
class FilteredSocketBalancer;
namespace NgHttp2 { class Stock; }

/**
 * A #ResourceLoader implementation which integrates all client-side
 * protocols implemented by beng-proxy.
 */
class DirectResourceLoader final : public ResourceLoader {
	EventLoop &event_loop;
#ifdef HAVE_URING
	Uring::Queue *const uring;
#endif
	TcpBalancer *tcp_balancer;
	FilteredSocketBalancer &fs_balancer;
#ifdef HAVE_NGHTTP2
	NgHttp2::Stock &nghttp2_stock;
#endif
	SpawnService &spawn_service;
	LhttpStock *lhttp_stock;
	FcgiStock *fcgi_stock;
#ifdef HAVE_LIBWAS
	WasStock *was_stock;
#endif
	StockMap *delegate_stock;
#ifdef HAVE_LIBNFS
	NfsCache *nfs_cache;
#endif

public:
	DirectResourceLoader(EventLoop &_event_loop,
#ifdef HAVE_URING
			     Uring::Queue *_uring,
#endif
			     TcpBalancer *_tcp_balancer,
			     FilteredSocketBalancer &_fs_balancer,
#ifdef HAVE_NGHTTP2
			     NgHttp2::Stock &_nghttp2_stock,
#endif
			     SpawnService &_spawn_service,
			     LhttpStock *_lhttp_stock,
			     FcgiStock *_fcgi_stock,
#ifdef HAVE_LIBWAS
			     WasStock *_was_stock,
#endif
			     StockMap *_delegate_stock
#ifdef HAVE_LIBNFS
			     , NfsCache *_nfs_cache
#endif
			     ) noexcept
		:event_loop(_event_loop),
#ifdef HAVE_URING
		 uring(_uring),
#endif
		 tcp_balancer(_tcp_balancer),
		 fs_balancer(_fs_balancer),
#ifdef HAVE_NGHTTP2
		 nghttp2_stock(_nghttp2_stock),
#endif
		 spawn_service(_spawn_service),
		 lhttp_stock(_lhttp_stock),
		 fcgi_stock(_fcgi_stock),
#ifdef HAVE_LIBWAS
		 was_stock(_was_stock),
#endif
		 delegate_stock(_delegate_stock)
#ifdef HAVE_LIBNFS
		, nfs_cache(_nfs_cache)
#endif
	{
	}

	/* virtual methods from class ResourceLoader */
	void SendRequest(struct pool &pool,
			 const StopwatchPtr &parent_stopwatch,
			 sticky_hash_t sticky_hash,
			 const char *cache_tag,
			 const char *site_name,
			 http_method_t method,
			 const ResourceAddress &address,
			 http_status_t status, StringMap &&headers,
			 UnusedIstreamPtr body, const char *body_etag,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};

#endif
