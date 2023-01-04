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

/*
 * Caching HTTP responses.  Implementation for the rules described in
 * RFC 2616.
 */

#pragma once

#include <cstdint>
#include <optional>

#include <sys/types.h> /* for off_t */

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;
class AllocatorPtr;
class StringMap;
struct ResourceAddress;
struct HttpCacheDocument;
struct HttpCacheRequestInfo;
struct HttpCacheResponseInfo;

/**
 * @param obey_no_cache if false, then "no-cache" requests will be
 * ignored
 */
[[gnu::pure]]
std::optional<HttpCacheRequestInfo>
http_cache_request_evaluate(HttpMethod method,
			    const ResourceAddress &address,
			    const StringMap &headers,
			    bool obey_no_cache,
			    bool has_request_body) noexcept;

[[gnu::pure]]
bool
http_cache_vary_fits(const StringMap &vary, const StringMap &headers) noexcept;

[[gnu::pure]]
bool
http_cache_vary_fits(const StringMap *vary, const StringMap &headers) noexcept;

/**
 * Check whether the request should invalidate the existing cache.
 */
bool
http_cache_request_invalidate(HttpMethod method) noexcept;

/**
 * Check whether the HTTP response should be put into the cache.
 */
[[gnu::pure]]
std::optional<HttpCacheResponseInfo>
http_cache_response_evaluate(const HttpCacheRequestInfo &request_info,
			     AllocatorPtr alloc,
			     bool eager_cache,
			     HttpStatus status, const StringMap &headers,
			     off_t body_available) noexcept;

/**
 * Copy all request headers mentioned in the Vary response header to a
 * new strmap.
 */
void
http_cache_copy_vary(StringMap &dest, AllocatorPtr alloc, const char *vary,
		     const StringMap &request_headers) noexcept;

/**
 * The server sent us a non-"Not Modified" response.  Check if we want
 * to serve the cache item anyway, and discard the server's response.
 */
[[gnu::pure]]
bool
http_cache_prefer_cached(const HttpCacheDocument &document,
			 const StringMap &response_headers) noexcept;
