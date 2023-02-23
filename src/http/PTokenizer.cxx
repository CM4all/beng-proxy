// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PTokenizer.hxx"
#include "Tokenizer.hxx"
#include "Chars.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

std::string_view
http_next_quoted_string(AllocatorPtr alloc, std::string_view &input) noexcept
{
	char *dest = alloc.NewArray<char>(input.size()); /* TODO: optimize memory consumption */
	std::size_t pos = 1, value_size = 0;

	while (pos < input.size()) {
		if (input[pos] == '\\') {
			++pos;
			if (pos < input.size())
				dest[value_size++] = input[pos++];
		} else if (input[pos] == '"') {
			++pos;
			break;
		} else if (char_is_http_text(input[pos])) {
			dest[value_size++] = input[pos++];
		} else {
			++pos;
		}
	}

	input = input.substr(pos);
	return {dest, value_size};
}

std::string_view
http_next_value(AllocatorPtr alloc, std::string_view &input) noexcept
{
	if (!input.empty() && input.front() == '"')
		return http_next_quoted_string(alloc, input);
	else
		return http_next_token(input);
}

std::pair<std::string_view, std::string_view>
http_next_name_value(AllocatorPtr alloc, std::string_view &input) noexcept
{
	const auto name = http_next_token(input);
	if (name.empty())
		return {name, {}};

	input = StripLeft(input);
	if (!input.empty() && input.front() == '=') {
		input = StripLeft(input.substr(1));

		return {name, http_next_value(alloc, input)};
	} else
		return {name, {}};
}
