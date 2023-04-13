// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/StringSplit.hxx"

#include <cassert>
#include <stdexcept>
#include <string_view>

/**
 * Throws std::runtime_error on error.
 */
template<typename Result, typename MatchData>
void
ExpandString(Result &result, std::string_view src, MatchData &&match_data)
{
	while (true) {
		const auto [a, b] = Split(src, '\\');

		/* copy everything up to the backslash */
		result.Append(a);

		if (b.data() == nullptr)
			return;

		if (b.empty())
			throw std::runtime_error{"Backslash at end of string"};

		/* now evaluate the escape */
		src = b.substr(1);
		const char ch = b.front();
		if (ch == '\\')
			result.Append(ch);
		else if (ch >= '0' && ch <= '9') {
			const std::size_t i = ch - '0';
			if (i >= match_data.size())
				throw std::runtime_error("Invalid regex capture");

			if (auto c = match_data[i]; !c.empty())
				result.AppendValue(c);
		} else {
			throw std::runtime_error{"Invalid backslash escape"};
		}
	}
}
