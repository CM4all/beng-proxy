// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "StringList.hxx"
#include "util/IterableSplitString.hxx"

bool
StringListContains(std::string_view haystack, char separator,
		   std::string_view needle) noexcept
{
	for (std::string_view i : IterableSplitString(haystack, separator))
		if (i == needle)
			return true;

	return false;
}
