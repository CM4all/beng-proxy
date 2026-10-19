// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CacheControl.hxx"
#include "http/CommonHeaders.hxx"
#include "http/List.hxx"
#include "util/IterableSplitString.hxx"
#include "util/NumberParser.hxx"
#include "util/StringCompare.hxx"
#include "util/StringStrip.hxx"
#include "strmap.hxx"

using std::string_view_literals::operator""sv;

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

bool
IsSharedCacheable(const StringMap &headers) noexcept
{
	/* may this response be shared with other clients even though
	   it sets a cookie? */
	bool explicitly_shareable = false;

	const auto r = headers.EqualRange(cache_control_header);
	for (auto i = r.first; i != r.second; ++i) {
		for (std::string_view s : IterableSplitString(i->value, ',')) {
			s = Strip(s);

			if (StringStartsWithIgnoreCase(s, "private"sv) ||
			    IsCacheControlDirective(s, "no-cache"sv) ||
			    IsCacheControlDirective(s, "no-store"sv))
				return false;

			else if (IsCacheControlDirective(s, "public"sv))
				explicitly_shareable = true;

			else if (SkipPrefixIgnoreCase(s, "s-maxage="sv) &&
				 ParseMaxAge(s).count() > 0)
				/* only a positive "s-maxage" permits
				   shared reuse */
				explicitly_shareable = true;
		}
	}

	if (!explicitly_shareable &&
	    (headers.Contains(set_cookie_header) ||
	     headers.Contains(set_cookie2_header)))
		/* this response sets a cookie, which means it is
		   probably personalized; don't store it in a shared
		   cache unless the origin explicitly allows it */
		return false;

	return true;
}
