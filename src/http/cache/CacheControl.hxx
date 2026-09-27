// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/CommonHeaders.hxx"
#include "http/List.hxx"
#include "strmap.hxx"

/**
 * Does any of the "Cache-Control" header lines contain the specified
 * directive?
 */
[[gnu::pure]]
inline bool
HasCacheControlDirective(const StringMap &headers,
			 std::string_view directive) noexcept
{
	const auto r = headers.EqualRange(cache_control_header);

	for (auto i = r.first; i != r.second; ++i)
		if (http_list_contains(i->value, directive))
			return true;

	return false;
}
