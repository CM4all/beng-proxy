// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CombinedHeaders.hxx"
#include "util/StringCompare.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <cassert>
#include <cstring> // for strlen(), stpcpy()

using std::string_view_literals::operator""sv;

std::string_view
GetCombinedHeader(AllocatorPtr alloc, const StringMap &map, StringMapKey key,
		  bool always_copy) noexcept
{
	const auto r = map.EqualRange(key);
	if (r.first == r.second)
		return {};

	if (std::next(r.first) == r.second) [[likely]] {
		std::string_view result{r.first->value};
		if (result.empty()) [[unlikely]]
			return {};

		if (always_copy)
			return {alloc.DupZ(result), result.size()};

		return result;
	}

	std::size_t total_size = 0;
	for (auto i = r.first; i != r.second; ++i)
		if (!StringIsEmpty(i->value))
			total_size += strlen(i->value) + 1uz;

	if (total_size == 0)
		/* all values are empty */
		return {};

	char *const result = alloc.NewArray<char>(total_size);
	char *p = result;

	for (auto i = r.first; i != r.second; ++i) {
		if (StringIsEmpty(i->value))
			continue;

		if (p != result)
			*p++ = ',';

		p = stpcpy(p, i->value);
	}

	assert(p == result + total_size - 1);

	*p = '\0';

	return {result, p};
}

bool
CombinedHeaderEquals(const StringMap &headers, StringMapKey key,
		     std::string_view expected) noexcept
{
	const auto r = headers.EqualRange(key);

	bool first = true;

	for (auto i = r.first; i != r.second; ++i) {
		const std::string_view value{i->value};
		if (value.empty())
			continue;

		if (!first && !SkipPrefix(expected, ","sv))
			return false;

		first = false;

		expected = StripLeft(expected);

		if (!SkipPrefix(expected, value))
			return false;
	}

	return expected.empty();
}
