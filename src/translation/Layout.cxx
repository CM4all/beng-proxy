// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Layout.hxx"
#include "lib/pcre/Cache.hxx"
#include "lib/pcre/Options.hxx"
#include "util/StringCompare.hxx"

#include <cassert>

TranslationLayoutItem::TranslationLayoutItem(Type _type,
					     std::string_view _value,
					     Pcre::Cache &pcre_cache) noexcept
	:value(_value), type(_type)
{
	if (type == Type::REGEX)
		regex = pcre_cache.Get(value, static_cast<int>(Pcre::CompileOptions{.anchored=true}));
}

bool
TranslationLayoutItem::Match(const char *uri) const noexcept
{
	switch (type) {
	case Type::EXACT:
		assert(!regex.IsDefined());

		return value == uri;

	case Type::BASE:
		assert(!regex.IsDefined());

		return StringStartsWith(uri, value);

	case Type::REGEX:
		assert(regex.IsDefined());

		return regex.Match(uri);
	}

	std::unreachable();
}
