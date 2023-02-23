// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
