// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CookieExtract.hxx"
#include "CookieString.hxx"
#include "util/StringStrip.hxx"

std::string_view
ExtractCookieRaw(std::string_view cookie_header, std::string_view name) noexcept
{
	std::string_view input = cookie_header;

	while (true) {
		const auto [current_name, current_value] =
			    cookie_next_name_value(input, true);
		if (current_name.empty())
			return {};

		if (current_name == name)
			return current_value;

		input = StripLeft(input);
		if (input.empty() || input.front() != ';')
			return {};

		input = StripLeft(input.substr(1));
	}
}
