// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "StringSet.hxx"
#include "util/StringAPI.hxx"

bool
StringSet::Contains(const char *p) const noexcept
{
	for (auto i : *this)
		if (StringIsEqual(i, p))
			return true;

	return false;
}
