// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef BENG_PROXY_EXPAND_HXX
#define BENG_PROXY_EXPAND_HXX

#include <stdexcept>

#include <assert.h>
#include <string.h>

/**
 * Throws std::runtime_error on error.
 */
template<typename Result, typename MatchData>
void
ExpandString(Result &result, const char *src, MatchData &&match_data)
{
	assert(src != nullptr);

	while (true) {
		const char *backslash = strchr(src, '\\');
		if (backslash == nullptr) {
			/* append the remaining input string and return */
			result.Append(src);
			return;
		}

		/* copy everything up to the backslash */
		result.Append(src, backslash - src);

		/* now evaluate the escape */
		src = backslash + 1;
		const char ch = *src++;
		if (ch == '\\')
			result.Append(ch);
		else if (ch >= '0' && ch <= '9') {
			const std::size_t i = ch - '0';
			if (i >= match_data.size())
				throw std::runtime_error("Invalid regex capture");

			if (auto c = match_data[i]; !c.empty())
				result.AppendValue(c.data(), c.size());
		} else {
			throw std::runtime_error{"Invalid backslash escape"};
		}
	}
}

#endif
