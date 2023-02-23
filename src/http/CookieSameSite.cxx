// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CookieSameSite.hxx"

#include <stdexcept>

using std::string_view_literals::operator""sv;

CookieSameSite
ParseCookieSameSite(std::string_view s)
{
	if (s == "default"sv)
		return CookieSameSite::DEFAULT;
	else if (s == "strict"sv)
		return CookieSameSite::STRICT;
	else if (s == "lax"sv)
		return CookieSameSite::LAX;
	else if (s == "none"sv)
		return CookieSameSite::NONE;
	else
		throw std::invalid_argument{"Invalid Cookie/SameSite attribute value"};
}
