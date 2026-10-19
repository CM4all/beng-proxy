// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class StringMap;

/**
 * Does any of the "Cache-Control" header lines contain the specified
 * directive?
 */
[[gnu::pure]]
bool
HasCacheControlDirective(const StringMap &headers,
			 std::string_view directive) noexcept;
