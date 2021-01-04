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

#include "CsrfToken.hxx"
#include "session/Id.hxx"
#include "sodium/GenericHash.hxx"
#include "util/HexFormat.h"

#include <algorithm>

#include <stdlib.h>
#include <string.h>

template<typename T>
static const char *
ParseHexSegment(const char *s, T &value_r) noexcept
{
	constexpr size_t segment_size = sizeof(T) * 2;

	if (memchr(s, 0, segment_size) != nullptr)
		/* too short */
		return nullptr;

	std::array<char, segment_size + 1> segment;
	*std::copy_n(s, segment_size, segment.begin()) = 0;

	char *endptr;
	value_r = strtoul(&segment.front(), &endptr, 16);
	if (endptr != &segment.back())
		return nullptr;

	return s + segment_size;
}

void
CsrfHash::Generate(std::chrono::system_clock::time_point time,
		   const SessionId &salt) noexcept
{
	const uint32_t t = ImportTime(time);

	/* calculate the Blake2b hash of the time stamp and the session's
	   salt */
	GenericHashState state(sizeof(data));
	state.UpdateT(t);
	state.UpdateT(salt);
	state.Final((unsigned char *)&data, sizeof(data));
}

const char *
CsrfHash::Parse(const char *s) noexcept
{
	for (auto &i : data) {
		s = ParseHexSegment(s, i);
		if (s == nullptr)
			break;
	}

	return s;
}

void
CsrfToken::Format(char *s) const noexcept
{
	format_uint32_hex_fixed(s, hash.ImportTime(time));
	s += 8;

	for (const uint8_t i : hash.data) {
		format_uint8_hex_fixed(s, i);
		s += 2;
	}

	*s = 0;
}

bool
CsrfToken::Parse(const char *s) noexcept
{
	if (s == nullptr)
		return false;

	uint32_t t;
	s = ParseHexSegment(s, t);
	if (s == nullptr)
		return false;

	time = hash.ExportTime(t);

	s = hash.Parse(s);
	return s != nullptr && *s == 0;
}
