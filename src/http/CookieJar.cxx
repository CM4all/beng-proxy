// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

/**
 * Compare two strings; both may be nullptr (which is the case for
 * cookies without a "Path" attribute).
 */
[[gnu::pure]]
static bool
StringIsEqualNullable(const char *a, const char *b) noexcept
{
	if (a == nullptr || b == nullptr)
		return a == b;

	return StringIsEqual(a, b);
}

[[gnu::pure]]
static Cookie *
Find(IntrusiveList<Cookie> &list, const char *domain,
     const char *path, const char *name) noexcept
{
	for (auto &i : list) {
		if (StringIsEqual(i.domain.c_str(), domain) &&
		    StringIsEqualNullable(i.path.c_str(), path) &&
		    StringIsEqual(i.name.c_str(), name))
			return &i;
	}

	return nullptr;
}

[[gnu::pure]]
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
