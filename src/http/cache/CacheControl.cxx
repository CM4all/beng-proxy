// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CacheControl.hxx"
#include "http/CommonHeaders.hxx"
#include "http/List.hxx"
#include "util/NumberParser.hxx"
#include "util/StringCompare.hxx"
#include "strmap.hxx"

bool
IsCacheControlDirective(std::string_view s, std::string_view name) noexcept
{
	return SkipPrefixIgnoreCase(s, name) && (s.empty() || s.front() == '=');
}

std::chrono::system_clock::duration
ParseMaxAge(std::string_view s) noexcept
{
	unsigned value;
	if (!ParseIntegerTo(s, value))
		return std::chrono::system_clock::duration{-1};

	return std::chrono::seconds{value};
}

bool
HasCacheControlDirective(const StringMap &headers,
			 std::string_view directive) noexcept
{
	const auto r = headers.EqualRange(cache_control_header);

	for (auto i = r.first; i != r.second; ++i)
		if (http_list_contains_i(i->value, directive))
			return true;

	return false;
}
