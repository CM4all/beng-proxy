/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

	return alloc.Dup(ConstBuffer<const char *>(tmp, num)).data;
}
