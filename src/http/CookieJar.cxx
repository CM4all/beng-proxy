// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CookieJar.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/StringAPI.hxx"
#include "util/StringCompare.hxx"

[[gnu::pure]]
static bool
domain_matches(const char *domain, const char *match) noexcept
{
	size_t domain_length = strlen(domain);
	size_t match_length = strlen(match);

	return domain_length >= match_length &&
		strcasecmp(domain + domain_length - match_length, match) == 0 &&
		(domain_length == match_length || /* "a.b" matches "a.b" */
		 match[0] == '.' || /* "a.b" matches ".b" */
		 /* "a.b" matches "b" (implicit dot according to RFC 2965
		    3.2.2): */
		 (domain_length > match_length &&
		  domain[domain_length - match_length - 1] == '.'));
}

[[gnu::pure]]
static bool
path_matches(const char *path, const char *match) noexcept
{
	assert(path != nullptr);

	return match == nullptr || StringStartsWith(path, match);
}

bool
CookieData::IsDomain(const char *request_domain) const noexcept
{
	assert(domain != nullptr);
	assert(request_domain != nullptr);

	return StringIsEqualIgnoreCase(domain.c_str(), request_domain);
}

bool
CookieData::DomainMatches(const char *request_domain) const noexcept
{
	assert(domain != nullptr);
	assert(request_domain != nullptr);

	return host_only
		? StringIsEqualIgnoreCase(request_domain, domain.c_str())
		: domain_matches(request_domain, domain.c_str());
}

bool
CookieData::IsPath(const char *request_path) const noexcept
{
	if (path == nullptr || request_path == nullptr)
		return path == nullptr && request_path == nullptr;

	return StringIsEqual(path.c_str(), request_path);
}

bool
CookieData::PathMatches(const char *request_path) const noexcept
{
	assert(request_path != nullptr);

	return path_matches(request_path, path.c_str());
}

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
