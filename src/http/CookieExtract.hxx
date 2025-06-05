// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

/**
 * Extract a cookie with a specific name from the "Cookie" request
 * header value.
 *
 * @param cookie_header the "Cookie" request header
 * @param name the cookie name to look for
 *
 * @return the raw (i.e. still quoted) cookie value or a
 * default-initialized std::string_view if no such cookie was found
 */
[[gnu::pure]]
std::string_view
ExtractCookieRaw(std::string_view cookie_header, std::string_view name) noexcept;
