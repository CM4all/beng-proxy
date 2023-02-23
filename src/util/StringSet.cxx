// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "StringSet.hxx"

#include <string.h>

bool
StringSet::Contains(const char *p) const noexcept
{
	for (auto i : *this)
		if (strcmp(i, p) == 0)
			return true;

	return false;
}
