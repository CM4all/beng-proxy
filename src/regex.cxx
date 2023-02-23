// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "regex.hxx"
#include "expand.hxx"
#include "lib/pcre/MatchData.hxx"

#include <assert.h>
#include <string.h>

std::size_t
ExpandStringLength(const char *src, const MatchData &match_data)
{
	struct Result {
		std::size_t result = 0;

		constexpr void Append(char) noexcept {
			++result;
		}

		void Append(const char *p) noexcept {
			result += strlen(p);
		}

		constexpr void Append(const char *,
				      std::size_t length) noexcept {
			result += length;
		}

		constexpr void AppendValue(const char *,
					   std::size_t length) noexcept {
			result += length;
		}

		constexpr size_t Commit() const noexcept {
			return result;
		}
	};

	Result result;
	ExpandString(result, src, match_data);
	return result.Commit();
}
