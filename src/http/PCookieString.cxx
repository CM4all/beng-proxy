// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PCookieString.hxx"
#include "CookieString.hxx"
#include "Tokenizer.hxx"
#include "PTokenizer.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

static std::string_view
cookie_next_value(AllocatorPtr alloc, std::string_view &input) noexcept
{
	if (!input.empty() && input.front() == '"')
		return http_next_quoted_string(alloc, input);
	else
		return cookie_next_unquoted_value(input);
}

static std::string_view
cookie_next_rfc_ignorant_value(AllocatorPtr alloc, std::string_view &input) noexcept
{
	if (!input.empty() && input.front() == '"')
		return http_next_quoted_string(alloc, input);
	else
		return cookie_next_rfc_ignorant_value(input);
}

std::pair<std::string_view, std::string_view>
cookie_next_name_value(AllocatorPtr alloc, std::string_view &input,
		       bool rfc_ignorant) noexcept
{
	const auto name = http_next_token(input);
	if (name.empty())
		return {name, {}};

	input = StripLeft(input);
	if (!input.empty() && input.front() == '=') {
		input = StripLeft(input.substr(1));

		const auto value = rfc_ignorant
			? cookie_next_rfc_ignorant_value(alloc, input)
			: cookie_next_value(alloc, input);
		return {name, value};
	} else
		return {name, {}};
}
