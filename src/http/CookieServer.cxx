// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CookieServer.hxx"
#include "Tokenizer.hxx"
#include "util/IterableSplitString.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

#include <cassert>

const char *
cookie_exclude(const char *const p, const std::string_view exclude,
	       AllocatorPtr alloc) noexcept
{
	assert(p != nullptr);

	char *const dest0 = alloc.NewArray<char>(strlen(p) + 1);
	char *dest = dest0;

	const std::string_view input = p;

	bool found = false;

	for (std::string_view i : IterableSplitString(input, ';')) {
		i = StripLeft(i);

		std::string_view tmp{i};
		const auto name = http_next_token(tmp);
		if (name == exclude) {
			if (!found) {
				dest = std::copy(input.begin(), i.begin(), dest);
				found = true;
			}
		} else if (found) {
			dest = std::copy(i.begin(), i.end(), dest);
			*dest++ = ';';
		}
	}

	if (!found)
		return p;

	if (dest == dest0)
		return nullptr;

	if (dest[-1] == ';')
		--dest;

	*dest = 0;
	return dest0;
}
