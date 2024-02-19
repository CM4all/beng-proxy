// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
[[nodiscard]] [[gnu::pure]]
std::optional<HttpCacheRequestInfo>
http_cache_request_evaluate(HttpMethod method,
			    const ResourceAddress &address,
			    const StringMap &headers,
			    bool obey_no_cache,
			    bool has_request_body) noexcept;

[[nodiscard]] [[gnu::pure]]
bool
http_cache_vary_fits(const StringMap &vary, const StringMap &headers) noexcept;

[[nodiscard]] [[gnu::pure]]
bool
http_cache_vary_fits(const StringMap *vary, const StringMap &headers) noexcept;

/**
 * Check whether the request should invalidate the existing cache.
 */
[[nodiscard]] [[gnu::const]]
bool
http_cache_request_invalidate(HttpMethod method) noexcept;

/**
 * Check whether the HTTP response should be put into the cache.
 */
[[nodiscard]] [[gnu::pure]]
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
[[nodiscard]] [[gnu::pure]]
StringMap
http_cache_copy_vary(AllocatorPtr alloc, const char *vary,
		     const StringMap &request_headers) noexcept;

/**
 * The server sent us a non-"Not Modified" response.  Check if we want
 * to serve the cache item anyway, and discard the server's response.
 */
[[nodiscard]] [[gnu::pure]]
bool
http_cache_prefer_cached(const HttpCacheDocument &document,
			 const StringMap &response_headers) noexcept;
