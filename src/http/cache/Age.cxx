// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Age.hxx"
#include "strmap.hxx"

static constexpr std::chrono::hours HOUR(1);
static constexpr std::chrono::hours DAY = 24 * HOUR;
static constexpr auto WEEK = 7 * DAY;

/**
 * Returns the upper "maximum age" limit.  If the server specifies a
 * bigger maximum age, it will be clipped at this return value.
 */
[[gnu::pure]]
static std::chrono::seconds
http_cache_age_limit(const StringMap &vary) noexcept
{
	if (vary.IsEmpty())
		return WEEK;

	/* if there's a "Vary" response header, we may assume that the
	   response is much more volatile, and lower limits apply */

	if (vary.Contains("x-cm4all-beng-user") ||
	    vary.Contains("cookie") ||
	    vary.Contains("cookie2"))
		/* this response is specific to this one authenticated
		   user, and caching it for a long time will not be
		   helpful */
		return std::chrono::minutes(5);

	if (vary.Contains("x-widgetid") || vary.Contains("x-widgethref"))
		/* this response is specific to one widget instance */
		return std::chrono::minutes(30);

	return std::chrono::hours(1);
}

std::chrono::steady_clock::time_point
http_cache_calc_expires(std::chrono::steady_clock::time_point steady_now,
			std::chrono::system_clock::time_point system_now,
			std::chrono::system_clock::time_point expires,
			const StringMap &vary) noexcept
{
	std::chrono::steady_clock::duration max_age;
	if (expires == std::chrono::system_clock::from_time_t(-1))
		/* there is no Expires response header; keep it in the cache
		   for 1 hour, but check with If-Modified-Since */
		max_age = std::chrono::hours(1);
	else {
		if (expires <= system_now)
			/* already expired, bail out */
			return {};

		max_age = std::chrono::duration_cast<std::chrono::steady_clock::duration>(expires - system_now);
	}

	const std::chrono::steady_clock::duration age_limit = http_cache_age_limit(vary);
	if (age_limit < max_age)
		max_age = age_limit;

	return steady_now + max_age;
}
