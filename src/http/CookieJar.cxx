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

#include "CookieJar.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/StringAPI.hxx"

CookieJar::CookieJar(const CookieJar &src)
{
	for (const auto &src_cookie : src.cookies) {
		auto *dest_cookie = new Cookie(src_cookie);
		Add(*dest_cookie);
	}
}

CookieJar::~CookieJar() noexcept
{
	cookies.clear_and_dispose(DeleteDisposer{});
}

void
CookieJar::EraseAndDispose(Cookie &cookie) noexcept
{
	cookie.unlink();
	delete &cookie;
}

void
CookieJar::Expire(Expiry now) noexcept

{
	cookies.remove_and_dispose_if([now](const Cookie &cookie){
		return cookie.expires.IsExpired(now);
	}, DeleteDisposer{});
}

gcc_pure
static Cookie *
Find(IntrusiveList<Cookie> &list, const char *domain,
     const char *path, const char *name) noexcept
{
	for (auto &i : list) {
		if (StringIsEqual(i.domain.c_str(), domain) &&
		    StringIsEqual(i.path.c_str(), path) &&
		    StringIsEqual(i.name.c_str(), name))
			return &i;
	}

	return nullptr;
}

gcc_pure
static Cookie *
Find(IntrusiveList<Cookie> &list, const Cookie &cookie) noexcept
{
	return Find(list, cookie.domain.c_str(), cookie.path.c_str(),
		    cookie.name.c_str());
}

void
CookieJar::MoveFrom(CookieJar &&src) noexcept
{
	src.cookies.clear_and_dispose([this](Cookie *i){
		auto *dest = Find(cookies, *i);
		if (dest != nullptr) {
			dest->unlink();
			delete dest;
		}

		cookies.push_back(*i);
	});
}
