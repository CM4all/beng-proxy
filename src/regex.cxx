// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "regex.hxx"
#include "expand.hxx"
#include "lib/pcre/MatchData.hxx"

std::size_t
ExpandStringLength(std::string_view src, const MatchData &match_data)
{
	struct Result {
		std::size_t result = 0;

		constexpr void Append(char) noexcept {
			++result;
		}

		constexpr void Append(std::string_view s) noexcept {
			result += s.size();
		}

		constexpr void AppendValue(std::string_view s) noexcept {
			result += s.size();
		}

		constexpr size_t Commit() const noexcept {
			return result;
		}
	};

	Result result;
	ExpandString(result, src, match_data);
	return result.Commit();
}
