// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Layout.hxx"
#include "util/StringCompare.hxx"

bool
TranslationLayoutItem::Match(const char *uri) const noexcept
{
	if (regex.IsDefined())
		return regex.Match(uri);

	return StringStartsWith(uri, value);
}
