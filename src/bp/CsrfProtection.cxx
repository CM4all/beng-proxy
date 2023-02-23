// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CsrfProtection.hxx"
#include "CsrfToken.hxx"
#include "Request.hxx"
#include "Instance.hxx"
#include "session/Lease.hxx"
#include "session/Session.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"

bool
Request::HasValidCsrfToken() noexcept
{
	CsrfToken given_csrf_token;
	if (!given_csrf_token.Parse(request.headers.Get("x-cm4all-csrf-token")))
		return false;

	constexpr std::chrono::system_clock::duration max_age =
		std::chrono::hours(1);
	const auto now = instance.event_loop.SystemNow();
	if (given_csrf_token.time > now ||
	    given_csrf_token.time < now - max_age)
		/* expired */
		return false;

	CsrfHash expected_hash;

	{
		const auto session = GetSession();
		if (!session)
			/* ignore this requirement if there is no
			   session */
			return true;

		expected_hash.Generate(given_csrf_token.time,
				       session->csrf_salt);
	}

	return expected_hash == given_csrf_token.hash;
}

bool
Request::CheckCsrfToken() noexcept
{
	bool result = HasValidCsrfToken();
	if (!result)
		DispatchError(HttpStatus::FORBIDDEN, "Bad CSRF token");
	return result;
}

void
Request::WriteCsrfToken(HttpHeaders &headers) noexcept
{
	CsrfToken token;

	{
		const auto session = GetSession();
		if (!session)
			/* need a valid session */
			return;

		token.Generate(instance.event_loop.SystemNow(), session->csrf_salt);
	}

	char s[CsrfToken::STRING_LENGTH + 1];
	token.Format(s);
	headers.Write("x-cm4all-csrf-token", s);
}
