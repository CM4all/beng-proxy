/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "CookieServer.hxx"
#include "PCookieString.hxx"
#include "strmap.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

StringMap
cookie_map_parse(AllocatorPtr alloc, std::string_view _input) noexcept
{
	StringMap cookies;

	std::string_view input = _input;

	while (true) {
		const auto [name, value] =
			cookie_next_name_value(alloc, input, true);
		if (name.empty())
			break;

		cookies.Add(alloc, alloc.DupZ(name), alloc.DupZ(value));

		input = StripLeft(input);
		if (input.empty() || input.front() != ';')
			break;

		input = StripLeft(input.substr(1));
	}

	return cookies;
}

const char *
cookie_exclude(const char *p, const char *_exclude,
	       AllocatorPtr alloc) noexcept
{
	assert(p != nullptr);
	assert(_exclude != nullptr);

	const char *const p0 = p;
	char *const dest0 = alloc.NewArray<char>(strlen(p) + 1);
	char *dest = dest0;

	std::string_view input = p;

	const std::string_view exclude = _exclude;
	const char *src = p;

	bool empty = true, found = false;

	while (true) {
		const auto [name, value] =
			    cookie_next_name_value(alloc, input, true);
		if (name.empty())
			break;

		const bool skip = name == exclude;
		if (skip) {
			found = true;
			dest = (char *)mempcpy(dest, src, name.data() - src);
		} else
			empty = false;

		input = StripLeft(input);
		if (input.empty() || input.front() != ';') {
			if (skip)
				src = input.data();
			break;
		}

		input = StripLeft(input.substr(1));

		if (skip)
			src = input.data();
	}

	if (!found)
		return p0;

	if (empty)
		return nullptr;

	dest = (char *)mempcpy(dest, src, input.data() - src);
	*dest = 0;
	return dest0;
}
