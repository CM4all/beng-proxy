// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Session.hxx"
#include "strmap.hxx"
#include "http/CommonHeaders.hxx"
#include "http/CookieExtract.hxx"
#include "util/HexParse.hxx"
#include "util/StringSplit.hxx"

sticky_hash_t
lb_session_get(const StringMap &request_headers, const char *cookie_name)
{
	const char *cookie = request_headers.Get(cookie_header);
	if (cookie == NULL)
		return 0;

	auto session = ExtractCookieRaw(cookie, cookie_name);
	session = Split(session, '/').first;

	constexpr auto n_digits = sizeof(sticky_hash_t) * 2;

	if (session.size() < n_digits)
		return {};

	/* only parse the lowest 32 bits */
	session = session.substr(session.size() - n_digits);

	uint32_t hash;
	if (!ParseLowerHexFixed(session, hash))
		return {};

	return hash;
}
