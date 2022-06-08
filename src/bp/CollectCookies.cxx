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

#include "Request.hxx"
#include "strmap.hxx"
#include "session/Lease.hxx"
#include "session/Session.hxx"
#include "http/CookieClient.hxx"

const char *
Request::GetCookieHost() const noexcept
{
	if (translate.response->cookie_host != nullptr)
		return translate.response->cookie_host;

	return translate.address.GetHostAndPort();
}

static void
ParseSetCookie(CookieJar &cookies,
	       StringMap::const_iterator begin,
	       StringMap::const_iterator end,
	       const char *host_and_port, const char *path) noexcept
{
	for (auto i = begin; i != end; ++i)
		cookie_jar_set_cookie2(cookies, i->value,
				       host_and_port, path);
}

void
Request::CollectCookies(const StringMap &headers) noexcept
{
	auto r = headers.EqualRange("set-cookie2");
	if (r.first == r.second) {
		r = headers.EqualRange("set-cookie");
		if (r.first == r.second)
			return;
	}

	const char *host_and_port = GetCookieHost();
	if (host_and_port == nullptr)
		return;

	const char *path = GetCookieURI();
	if (path == nullptr)
		return;

	if (auto session = GetRealmSession()) {
		/* there's already an existing session */
		ParseSetCookie(session->cookies, r.first, r.second,
			       host_and_port, path);
		return;
	}

	/* there's no session yet; first parse the cookies, and see if
	   there is really a cookie in those headers */
	CookieJar cookies;
	ParseSetCookie(cookies, r.first, r.second,
		       host_and_port, path);
	if (cookies.empty())
		/* nah, we don't need a session */
		return;

	/* aye, create a session and move the cookie jar into it */
	if (auto session = MakeRealmSession())
		session->cookies = std::move(cookies);
}
