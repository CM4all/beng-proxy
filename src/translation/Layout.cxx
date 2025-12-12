// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Layout.hxx"
#include "util/StringCompare.hxx"

#include <cassert>

bool
TranslationLayoutItem::Match(const char *uri) const noexcept
{
	switch (type) {
	case Type::BASE:
		assert(!regex.IsDefined());

		return StringStartsWith(uri, value);

	case Type::REGEX:
		assert(regex.IsDefined());

		return regex.Match(uri);
	}

	std::unreachable();
}
