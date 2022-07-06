/*
 * Copyright 2007-2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

std::string_view
cookie_next_unquoted_value(std::string_view &input) noexcept
{
	std::size_t i = 0;
	while (i < input.size() &&
	       char_is_cookie_octet(input[i]))
		++i;

	auto p = Partition(input, i);
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

std::string_view
cookie_next_rfc_ignorant_value(std::string_view &input) noexcept
{
	std::size_t i = 0;
	while (i < input.size() &&
	       char_is_rfc_ignorant_cookie_octet(input[i]))
		++i;

	auto p = Partition(input, i);
	input = p.second;
	return p.first;
}

static std::string_view
cookie_next_value_raw(std::string_view &input) noexcept
{
	if (!input.empty() && input.front() == '"')
		return http_next_quoted_string_raw(input);
	else
		return cookie_next_unquoted_value(input);
}

static std::string_view
cookie_next_rfc_ignorant_value_raw(std::string_view &input) noexcept
{
	if (!input.empty() && input.front() == '"')
		return http_next_quoted_string_raw(input);
	else
		return cookie_next_rfc_ignorant_value(input);
}

std::pair<std::string_view, std::string_view>
cookie_next_name_value_raw(std::string_view &input, bool rfc_ignorant) noexcept
{
	const auto name = http_next_token(input);
	if (name.empty())
		return {name, nullptr};

	input = StripLeft(input);
	if (!input.empty() && input.front() == '=') {
		input = StripLeft(input.substr(1));

		const auto value = rfc_ignorant
			? cookie_next_rfc_ignorant_value_raw(input)
			: cookie_next_value_raw(input);
		return {name, value};
	} else
		return {name, nullptr};
}
