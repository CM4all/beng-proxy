// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CookieString.hxx"
#include "Tokenizer.hxx"
#include "util/StringSplit.hxx"
#include "util/StringStrip.hxx"

[[gnu::always_inline]]
static constexpr bool
char_is_cookie_octet(char ch) noexcept
{
	return ch == 0x21 || (ch >= 0x23 && ch <= 0x2b) ||
		(ch >= 0x2d && ch <= 0x3a) ||
		(ch >= 0x3c && ch <= 0x5b) ||
		(ch >= 0x5d && ch <= 0x7e);
}

static std::string_view
cookie_next_unquoted_value(std::string_view &input) noexcept
{
	auto p = SplitWhile(input, char_is_cookie_octet);
	input = p.second;
	return p.first;
}

[[gnu::always_inline]]
static constexpr bool
char_is_rfc_ignorant_cookie_octet(char ch) noexcept
{
	return char_is_cookie_octet(ch) ||
		ch == ' ' || ch == ',';
}

static std::string_view
cookie_next_rfc_ignorant_value_raw(std::string_view &input) noexcept
{
	auto p = SplitWhile(input, char_is_rfc_ignorant_cookie_octet);
	input = p.second;
	return p.first;
}

static std::string_view
cookie_next_value(std::string_view &input) noexcept
{
	if (!input.empty() && input.front() == '"')
		return http_next_quoted_string_raw(input);
	else
		return cookie_next_unquoted_value(input);
}

static std::string_view
cookie_next_rfc_ignorant_value(std::string_view &input) noexcept
{
	if (!input.empty() && input.front() == '"')
		return http_next_quoted_string_raw(input);
	else
		return cookie_next_rfc_ignorant_value_raw(input);
}

std::pair<std::string_view, std::string_view>
cookie_next_name_value(std::string_view &input, bool rfc_ignorant) noexcept
{
	const auto name = http_next_token(input);
	if (name.empty())
		return {name, {}};

	input = StripLeft(input);
	if (!input.empty() && input.front() == '=') {
		input = StripLeft(input.substr(1));

		const auto value = rfc_ignorant
			? cookie_next_rfc_ignorant_value(input)
			: cookie_next_value(input);
		return {name, value};
	} else
		return {name, {}};
}
