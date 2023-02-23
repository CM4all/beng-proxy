// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Compare.hxx"
#include "util/HexParse.hxx"

const char *
UriFindUnescapedSuffix(const std::string_view uri_start,
		       const std::string_view suffix_start) noexcept
{
	auto uri_i = uri_start.end();
	auto suffix_i = suffix_start.end();

	while (true) {
		if (suffix_i == suffix_start.begin())
			/* full match - success */
			return uri_i;

		if (uri_i == uri_start.begin())
			/* URI is too short - fail */
			return nullptr;

		--uri_i;
		--suffix_i;

		char suffix_ch = *suffix_i;

		if (suffix_ch == '%')
			/* malformed escape */
			return nullptr;

		if (suffix_start.begin() + 2 <= suffix_i &&
		    suffix_i[-2] == '%') {
			const int digit1 = ParseHexDigit(suffix_ch);
			if (digit1 < 0)
				/* malformed hex digit */
				return nullptr;

			const int digit2 = ParseHexDigit(*--suffix_i);
			if (digit2 < 0)
				/* malformed hex digit */
				return nullptr;

			--suffix_i;
			suffix_ch = (digit2 << 4) | digit1;
		}

		if (*uri_i != suffix_ch)
			/* mismatch */
			return nullptr;
	}
}
