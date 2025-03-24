// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Cookie.hxx"
#include "http/CommonHeaders.hxx"
#include "http/CookieExtract.hxx"
#include "util/NumberParser.hxx"
#include "util/StringCompare.hxx"
#include "strmap.hxx"

#include <stdlib.h> // for random()

using std::string_view_literals::operator""sv;

sticky_hash_t
lb_cookie_get(const StringMap &request_headers) noexcept
{
	const char *cookie = request_headers.Get(cookie_header);
	if (cookie == NULL)
		return 0;

	std::string_view s = ExtractCookieRaw(cookie, "beng_lb_node"sv);
	if (!SkipPrefix(s, "0-"sv))
		return 0;

	return ParseInteger<sticky_hash_t>(s, 16).value_or(0);
}

sticky_hash_t
lb_cookie_generate(unsigned n) noexcept
{
	assert(n >= 2);

	return (random() % n) + 1;
}
