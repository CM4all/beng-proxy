// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class AllocatorPtr;
class StringMap;
struct CookieJar;

/**
 * Parse a Set-Cookie2 response header and insert new cookies into the
 * linked list.
 *
 * @param path the URI path, used for verification; if nullptr, all
 * cookie paths are accepted
 */
void
cookie_jar_set_cookie2(CookieJar &jar, const char *value,
		       const char *domain, const char *path) noexcept;

/**
 * Generate the HTTP request header for cookies in the jar.
 */
[[gnu::pure]]
const char *
cookie_jar_http_header_value(const CookieJar &jar,
			     const char *domain, const char *path,
			     AllocatorPtr alloc) noexcept;

/**
 * Generate HTTP request headers passing for all cookies in the linked
 * list.
 */
void
cookie_jar_http_header(const CookieJar &jar,
		       const char *domain, const char *path,
		       StringMap &headers, AllocatorPtr alloc) noexcept;
