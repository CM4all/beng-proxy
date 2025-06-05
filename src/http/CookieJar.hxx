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

	template<typename N, typename V>
	CookieData(N &&_name, V &&_value)
		:name(std::forward<N>(_name)),
		 value(std::forward<V>(_value)) {}
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
