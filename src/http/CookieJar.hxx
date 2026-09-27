// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "time/Expiry.hxx"
#include "util/AllocatedString.hxx"
#include "util/IntrusiveList.hxx"

struct CookieData {
	const AllocatedString name;
	const AllocatedString value;
	AllocatedString domain, path;
	Expiry expires = Expiry::Never();

	/**
	 * Was this cookie received without a "Domain" attribute?  If
	 * so, it may only be sent to the exact host which has set it
	 * (RFC 6265 5.3).
	 */
	bool host_only;

	template<typename N, typename V>
	CookieData(N &&_name, V &&_value)
		:name(std::forward<N>(_name)),
		 value(std::forward<V>(_value)) {}

	/**
	 * Is this cookie exactly the specified domain?
	 *
	 * @param request_domain the domain to compare with (not nullptr)
	 */
	[[gnu::pure]]
	bool IsDomain(const char *request_domain) const noexcept;

	/**
	 * Does the given cookie apply to a request to the given host?
	 *
	 * @param request_domain the domain to compare with (not nullptr)
	 */
	[[gnu::pure]]
	bool DomainMatches(const char *request_domain) const noexcept;

	/**
	 * Is this cookie exactly the specified path?
	 *
	 * @param request_path the path to compare with; may be nullptr
	 */
	[[gnu::pure]]
	bool IsPath(const char *request_path) const noexcept;

	/**
	 * Does the given cookie apply to a request to the given path?
	 *
	 * @param request_path the path to compare with (not nullptr)
	 */
	[[gnu::pure]]
	bool PathMatches(const char *request_path) const noexcept;
};

struct Cookie : IntrusiveListHook<IntrusiveHookMode::NORMAL>, CookieData {
	/* this copy constructor is needed because the
	   IntrusiveListHook base class is not copyable */
	Cookie(const Cookie &src) noexcept
		:CookieData(src) {}

	template<typename N, typename V>
	Cookie(N &&_name, V &&_value) noexcept
		:CookieData(std::forward<N>(_name),
			    std::forward<V>(_value)) {}
};

/**
 * Container for cookies received from other HTTP servers.
 */
struct CookieJar {
	IntrusiveList<Cookie> cookies;

	CookieJar() = default;
	CookieJar(CookieJar &&) noexcept = default;

	CookieJar(const CookieJar &src);
	~CookieJar() noexcept;

	CookieJar &operator=(CookieJar &&src) noexcept {
		using std::swap;
		swap(cookies, src.cookies);
		return *this;
	}

	bool empty() const noexcept {
		return cookies.empty();
	}

	void Add(Cookie &cookie) noexcept {
		cookies.push_front(cookie);
	}

	void EraseAndDispose(Cookie &cookie) noexcept;

	/**
	 * Delete expired cookies.
	 */
	void Expire(Expiry now) noexcept;

	/**
	 * Move cookies from the given instance, overwriting existing
	 * cookies in this instance.
	 */
	void MoveFrom(CookieJar &&src) noexcept;
};
