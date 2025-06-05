// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Quote.hxx"
#include "Chars.hxx"

bool
http_must_quote_token(std::string_view src) noexcept
{
	for (auto ch : src)
		if (!char_is_http_token(ch))
			return true;
	return false;
}

std::size_t
http_quote_string(char *dest, const std::string_view src) noexcept
{
	size_t dest_pos = 0, src_pos = 0;

	dest[dest_pos++] = '"';

	while (src_pos < src.size()) {
		if (src[src_pos] == '"' || src[src_pos] == '\\') {
			dest[dest_pos++] = '\\';
			dest[dest_pos++] = src[src_pos++];
		} else if (char_is_http_text(src[src_pos]))
			dest[dest_pos++] = src[src_pos++];
		else
			/* ignore invalid characters */
			++src_pos;
	}

	dest[dest_pos++] = '"';
	return dest_pos;
}
