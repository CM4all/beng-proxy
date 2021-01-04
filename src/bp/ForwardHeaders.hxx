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

/*
 * Which headers should be forwarded to/from remote HTTP servers?
 */

#pragma once

#include "http/Status.h"

#include <beng-proxy/Headers.hxx>

class AllocatorPtr;

struct HeaderForwardSettings {
	using Group = BengProxy::HeaderGroup;
	using Mode = BengProxy::HeaderForwardMode;

	Mode modes[size_t(Group::MAX)];

	static constexpr HeaderForwardSettings AllNo() noexcept {
		static_assert(Mode::NO == Mode(),
			      "Wrong default value");

		return HeaderForwardSettings{Mode::NO};
	}

	constexpr auto &operator[](Group group) noexcept {
		return modes[size_t(group)];
	}

	constexpr const auto &operator[](Group group) const noexcept {
		return modes[size_t(group)];
	}

	constexpr bool IsCookieMangle() const noexcept {
		return (*this)[Group::COOKIE] == Mode::MANGLE;
	}

	static constexpr auto MakeDefaultRequest() noexcept {
		auto s = AllNo();
		s[Group::IDENTITY] = Mode::MANGLE;
		s[Group::CAPABILITIES] = Mode::YES;
		s[Group::COOKIE] = Mode::MANGLE;
		return s;
	}

	static constexpr auto MakeDefaultResponse() noexcept {
		auto s = AllNo();
		s[Group::CAPABILITIES] = Mode::YES;
		s[Group::COOKIE] = Mode::MANGLE;
		s[Group::TRANSFORMATION] = Mode::MANGLE;
		s[Group::LINK] = Mode::YES;
		s[Group::AUTH] = Mode::MANGLE;
		return s;
	}
};

class StringMap;
struct RealmSession;

/**
 * @param exclude_host suppress the "Host" header?  The "Host" request
 * header must not be forwarded to another HTTP server, because we
 * need to generate a new one
 * @param forward_range forward the "Range" request header?
 */
StringMap
forward_request_headers(AllocatorPtr alloc, const StringMap &src,
			const char *local_host, const char *remote_host,
			const char *peer_subject,
			const char *peer_issuer_subject,
			bool exclude_host,
			bool with_body, bool forward_charset,
			bool forward_encoding,
			bool forward_range,
			const HeaderForwardSettings &settings,
			const char *session_cookie,
			const RealmSession *session,
			const char *user,
			const char *host_and_port, const char *uri) noexcept;

StringMap
forward_response_headers(AllocatorPtr alloc, http_status_t status,
			 const StringMap &src,
			 const char *local_host,
			 const char *session_cookie,
			 const char *(*relocate)(const char *uri, void *ctx) noexcept,
			 void *relocate_ctx,
			 const HeaderForwardSettings &settings) noexcept;

/**
 * Generate a X-CM4all-BENG-User header (if available)_.
 */
void
forward_reveal_user(AllocatorPtr alloc, StringMap &headers,
		    const char *user) noexcept;
