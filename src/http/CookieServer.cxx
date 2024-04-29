// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CookieServer.hxx"
#include "PCookieString.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

#include <cassert>

const char *
cookie_exclude(const char *p, const char *_exclude,
	       AllocatorPtr alloc) noexcept
{
	assert(p != nullptr);
	assert(_exclude != nullptr);

	const char *const p0 = p;
	char *const dest0 = alloc.NewArray<char>(strlen(p) + 1);
	char *dest = dest0;

	std::string_view input = p;

	const std::string_view exclude = _exclude;
	const char *src = p;

	bool empty = true, found = false;

	while (true) {
		const auto [name, value] =
			    cookie_next_name_value(alloc, input, true);
		if (name.empty())
			break;

		const bool skip = name == exclude;
		if (skip) {
			found = true;
			dest = (char *)mempcpy(dest, src, name.data() - src);
		} else
			empty = false;

		input = StripLeft(input);
		if (input.empty() || input.front() != ';') {
			if (skip)
				src = input.data();
			break;
		}

		input = StripLeft(input.substr(1));

		if (skip)
			src = input.data();
	}

	if (!found)
		return p0;

	if (empty)
		return nullptr;

	dest = (char *)mempcpy(dest, src, input.data() - src);
	*dest = 0;
	return dest0;
}
