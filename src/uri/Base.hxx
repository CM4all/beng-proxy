// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Functions for working with base URIs.
 */

#pragma once

#include <cstddef>
#include <string_view>

/**
 * Calculate the URI tail after a base URI from a request URI.
 * Returns nullptr if no such tail URI is possible (e.g. if the
 * specified URI is not "within" the base, or if there is no base at
 * all).
 *
 * @param uri the URI specified by the tcache client, may be nullptr
 * @param base the base URI, as given by the translation server,
 * stored in the cache item
 */
[[gnu::pure]]
const char *
base_tail(const char *uri, std::string_view base) noexcept;

/**
 * Similar to base_tail(), but assert that there is a base match.
 */
[[gnu::pure]]
const char *
require_base_tail(const char *uri, std::string_view base) noexcept;

/**
 * Determine the length of the base prefix in the given string.
 *
 * @return (std::size_t)-1 on mismatch
 */
[[gnu::pure]]
std::size_t
base_string(std::string_view uri, std::string_view tail) noexcept;

/**
 * Is the given string a valid base string?  That is, does it end with
 * a slash?
 */
[[gnu::pure]]
bool
is_base(std::string_view uri) noexcept;
