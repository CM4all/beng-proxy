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

/*
 * Caching HTTP responses.  Memcached backend.
 */

#ifndef BENG_PROXY_HTTP_CACHE_MEMCACHED_HXX
#define BENG_PROXY_HTTP_CACHE_MEMCACHED_HXX

#include "http/Status.h"

#include <exception>

struct pool;
class StringMap;
class Istream;
struct MemachedStock;
class BackgroundManager;
class CancellablePointer;
struct HttpCacheResponseInfo;
struct HttpCacheDocument;

typedef void (*http_cache_memcached_flush_t)(bool success,
                                             std::exception_ptr ep, void *ctx);

typedef void (*http_cache_memcached_get_t)(HttpCacheDocument *document,
                                           Istream *body,
                                           std::exception_ptr ep, void *ctx);

typedef void (*http_cache_memcached_put_t)(std::exception_ptr ep, void *ctx);

void
http_cache_memcached_flush(struct pool &pool, MemachedStock &stock,
                           http_cache_memcached_flush_t callback,
                           void *callback_ctx,
                           CancellablePointer &cancel_ptr);

void
http_cache_memcached_get(struct pool &pool, MemachedStock &stock,
                         struct pool &background_pool,
                         BackgroundManager &background,
                         const char *uri, StringMap &request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr);

void
http_cache_memcached_put(struct pool &pool, MemachedStock &stock,
                         struct pool &background_pool,
                         BackgroundManager &background,
                         const char *uri,
                         const HttpCacheResponseInfo &info,
                         const StringMap &request_headers,
                         http_status_t status,
                         const StringMap *response_headers,
                         Istream *value,
                         http_cache_memcached_put_t put, void *callback_ctx,
                         CancellablePointer &cancel_ptr);

void
http_cache_memcached_remove_uri(MemachedStock &stock,
                                struct pool &background_pool,
                                BackgroundManager &background,
                                const char *uri);

void
http_cache_memcached_remove_uri_match(MemachedStock &stock,
                                      struct pool &background_pool,
                                      BackgroundManager &background,
                                      const char *uri, StringMap &headers);

#endif
