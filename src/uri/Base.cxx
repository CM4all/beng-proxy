// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Base.hxx"
#include "util/StringCompare.hxx"

#include <cassert>

const char *
base_tail(const char *uri, std::string_view base) noexcept
{
	assert(uri != nullptr);

	if (!is_base(base))
		/* not a valid base */
		return nullptr;

	return StringAfterPrefix(uri, base);
}

const char *
require_base_tail(const char *uri, std::string_view base) noexcept
{
	assert(uri != nullptr);
	assert(is_base(base));
	assert(StringStartsWith(uri, base));

	return uri + base.size();
}

std::size_t
base_string(std::string_view uri, std::string_view tail) noexcept
{
	if (uri.size() == tail.size())
		/* special case: zero-length prefix (not followed by a
		   slash) */
		return uri == tail ? 0 : (std::size_t)-1;

	return uri.size() > tail.size() &&
		uri[uri.size() - tail.size() - 1] == '/' &&
		uri.ends_with(tail)
		? uri.size() - tail.size()
		: (std::size_t)-1;
}

bool
is_base(std::string_view uri) noexcept
{
	return uri.ends_with('/');
}
