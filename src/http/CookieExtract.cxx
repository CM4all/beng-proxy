// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CookieExtract.hxx"
#include "CookieString.hxx"
#include "Tokenizer.hxx"
#include "util/IterableSplitString.hxx"
#include "util/StringStrip.hxx"

std::string_view
ExtractCookieRaw(std::string_view cookie_header, std::string_view name) noexcept
{
	for (std::string_view i : IterableSplitString(cookie_header, ';')) {
		i = StripLeft(i);

		const auto current_name = http_next_token(i);
		if (current_name == name) {
			if (i.empty())
				return i;

			if (i.front() != '=')
				return {};

			i.remove_prefix(1);
			return cookie_next_rfc_ignorant_value(i);
		}
	}

	return {};
}
