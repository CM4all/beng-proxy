// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "pexpand.hxx"
#include "expand.hxx"
#include "AllocatorPtr.hxx"
#include "regex.hxx"
#include "uri/Unescape.hxx"
#include "lib/pcre/MatchData.hxx"

#include <cassert>

const char *
expand_string(AllocatorPtr alloc, std::string_view src,
	      const MatchData &match_data)
{
	assert(match_data);

	const size_t length = ExpandStringLength(src, match_data);
	const auto buffer = alloc.NewArray<char>(length + 1);

	struct Result {
		char *q;

		constexpr void Append(char ch) noexcept {
			*q++ = ch;
		}

		constexpr void Append(std::string_view s) noexcept {
			q = std::copy(s.begin(), s.end(), q);
		}

		constexpr void AppendValue(std::string_view s) noexcept {
			Append(s);
		}
	};

	Result result{buffer};
	ExpandString(result, src, match_data);

	assert(result.q == buffer + length);
	*result.q = 0;

	return buffer;
}

const char *
expand_string_unescaped(AllocatorPtr alloc, std::string_view src,
			const MatchData &match_data)
{
	assert(match_data);

	const size_t length = ExpandStringLength(src, match_data);
	const auto buffer = alloc.NewArray<char>(length + 1);

	struct Result {
		char *q;

		constexpr void Append(char ch) noexcept {
			*q++ = ch;
		}

		constexpr void Append(std::string_view s) noexcept {
			q = std::copy(s.begin(), s.end(), q);
		}

		void AppendValue(std::string_view s) {
			q = UriUnescape(q, s);
			if (q == nullptr)
				throw std::runtime_error("Malformed URI escape");
		}
	};

	Result result{buffer};
	ExpandString(result, src, match_data);

	assert(result.q <= buffer + length);
	*result.q = 0;

	return buffer;
}
