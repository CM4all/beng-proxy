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

#include "CsrfToken.hxx"
#include "session/Id.hxx"
#include "lib/sodium/GenericHash.hxx"
#include "util/HexFormat.hxx"
#include "util/HexParse.hxx"

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
	state.Final(data);
}

const char *
CsrfHash::Parse(const char *s) noexcept
{
	return ParseLowerHexFixed(s, data);
}

void
CsrfToken::Format(char *s) const noexcept
{
	s = HexFormatUint32Fixed(s, hash.ImportTime(time));
	s = HexFormat(s, hash.data);
	*s = 0;
}

bool
CsrfToken::Parse(const char *s) noexcept
{
	if (s == nullptr)
		return false;

	uint32_t t;
	s = ParseLowerHexFixed(s, t);
	if (s == nullptr)
		return false;

	time = hash.ExportTime(t);

	s = hash.Parse(s);
	return s != nullptr && *s == 0;
}
