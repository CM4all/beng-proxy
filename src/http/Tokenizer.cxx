// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Tokenizer.hxx"
#include "Chars.hxx"
#include "util/StringSplit.hxx"

#include <cassert>

std::string_view
http_next_token(std::string_view &input) noexcept
{
	auto p = SplitWhile(input, char_is_http_token);
	input = p.second;
	return p.first;
}

std::string_view
http_next_quoted_string_raw(std::string_view &input) noexcept
{
	assert(input.starts_with('"'));
	input = input.substr(1);

	const auto [value, rest] = Split(input, '"');
	/* if there is no closing quote, we ignore it and make the
	   best of it */
	input = rest;
	return value;
}
