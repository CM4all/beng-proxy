// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PList.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

const char *const*
http_list_split(AllocatorPtr alloc, const char *p) noexcept
{
	constexpr size_t MAX_ITEMS = 64;
	const char *tmp[MAX_ITEMS + 1]; /* XXX dynamic allocation */
	size_t num = 0;

	do {
		const char *comma, *end;

		/* skip whitespace */
		p = StripLeft(p);

		if (*p == 0)
			break;

		/* find the next delimiter */
		end = comma = strchr(p, ',');
		if (end == nullptr)
			/* last element */
			end = p + strlen(p);

		/* delete trailing whitespace */
		end = StripRight(p, end);

		/* append new list item */
		tmp[num++] = alloc.DupToLower({p, end});

		if (comma == nullptr)
			/* this was the last element */
			break;

		/* continue after the comma */
		p = comma + 1;
	} while (num < MAX_ITEMS);

	tmp[num++] = nullptr;

	return alloc.Dup(std::span<const char *const>{tmp, num}).data();
}
