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
			/* need a valid session */
			return false;

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
		DispatchError(HTTP_STATUS_FORBIDDEN, "Bad CSRF token");
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

	char s[CsrfToken::STRING_LENGTH];
	token.Format(s);
	headers.Write("x-cm4all-csrf-token", s);
}
