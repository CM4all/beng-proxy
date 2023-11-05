// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
	       StringMap::equal_iterator begin,
	       StringMap::equal_iterator end,
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
