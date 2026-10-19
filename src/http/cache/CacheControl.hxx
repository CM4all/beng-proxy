// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <chrono>
#include <string_view>

class StringMap;

/**
 * Does the given Cache-Control directive have the specified name?
 *
 * @see RFC 9111 5.2
 */
[[gnu::pure]]
bool
IsCacheControlDirective(std::string_view s, std::string_view name) noexcept;

/**
 * Parse the argument of a "max-age" Cache-Control directive.
 *
 * Returns a negative value on error.
 */
[[gnu::pure]]
std::chrono::system_clock::duration
ParseMaxAge(std::string_view s) noexcept;

/**
 * Does any of the "Cache-Control" header lines contain the specified
 * directive?
 */
[[gnu::pure]]
bool
HasCacheControlDirective(const StringMap &headers,
			 std::string_view directive) noexcept;

/**
 * May this response (and everything derived from it, e.g. its filtered
 * or compressed form) be stored in a cache which is shared by all
 * users?
 *
 * @see RFC 9111 5.2.2.7
 */
[[gnu::pure]]
bool
IsSharedCacheable(const StringMap &headers) noexcept;
