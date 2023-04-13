// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "pexpand.hxx"
#include "expand.hxx"
#include "AllocatorPtr.hxx"
#include "regex.hxx"
#include "uri/Unescape.hxx"
#include "lib/pcre/MatchData.hxx"

#include <assert.h>
#include <string.h>

const char *
expand_string(AllocatorPtr alloc, const char *src,
	      const MatchData &match_data)
{
	assert(src != nullptr);
	assert(match_data);

	const size_t length = ExpandStringLength(src, match_data);
	const auto buffer = alloc.NewArray<char>(length + 1);

	struct Result {
		char *q;

		constexpr void Append(char ch) noexcept {
			*q++ = ch;
		}

		void Append(const char *p) noexcept {
			q = stpcpy(q, p);
		}

		void Append(const char *p, size_t _length) noexcept {
			q = (char *)mempcpy(q, p, _length);
		}

		void AppendValue(const char *p, size_t _length) noexcept {
			Append(p, _length);
		}
	};

	Result result{buffer};
	ExpandString(result, src, match_data);

	assert(result.q == buffer + length);
	*result.q = 0;

	return buffer;
}

const char *
expand_string_unescaped(AllocatorPtr alloc, const char *src,
			const MatchData &match_data)
{
	assert(src != nullptr);
	assert(match_data);

	const size_t length = ExpandStringLength(src, match_data);
	const auto buffer = alloc.NewArray<char>(length + 1);

	struct Result {
		char *q;

		constexpr void Append(char ch) noexcept {
			*q++ = ch;
		}

		void Append(const char *p) noexcept {
			q = stpcpy(q, p);
		}

		void Append(const char *p, size_t _length) noexcept {
			q = (char *)mempcpy(q, p, _length);
		}

		void AppendValue(const char *p, size_t _length) {
			q = UriUnescape(q, {p, _length});
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
