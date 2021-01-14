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

#pragma once

#include "util/Expiry.hxx"
#include "util/AllocatedString.hxx"
#include "util/StringView.hxx"
#include "util/IntrusiveList.hxx"

struct Cookie : IntrusiveListHook {
	const AllocatedString name;
	const AllocatedString value;
	AllocatedString domain, path;
	Expiry expires = Expiry::Never();

	template<typename N, typename V>
	Cookie(N &&_name, V &&_value)
		:name(std::forward<N>(_name)),
		 value(std::forward<V>(_value)) {}
};

/**
 * Container for cookies received from other HTTP servers.
 */
struct CookieJar {
	IntrusiveList<Cookie> cookies;

	CookieJar() = default;

	CookieJar(const CookieJar &src);
	~CookieJar() noexcept;

	void Add(Cookie &cookie) noexcept {
		cookies.push_front(cookie);
	}

	void EraseAndDispose(Cookie &cookie) noexcept;

	/**
	 * Delete expired cookies.
	 */
	void Expire(Expiry now) noexcept;
};
