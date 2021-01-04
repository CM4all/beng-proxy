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

#include "util/Compiler.h"

#include <nghttp2/nghttp2.h>

#include <algorithm>

namespace NgHttp2 {

class Session {
	nghttp2_session *session = nullptr;

public:
	Session() noexcept = default;

	~Session() noexcept {
		if (session != nullptr)
			nghttp2_session_del(session);
	}

	Session(Session &&src) noexcept
		:session(std::exchange(src.session, nullptr)) {}

	Session &operator=(Session &&src) noexcept {
		using std::swap;
		swap(session, src.session);
		return *this;
	}

	static Session NewServer(const nghttp2_session_callbacks *callbacks,
				 void *user_data,
				 const nghttp2_option *option) noexcept {
		Session session;
		nghttp2_session_server_new2(&session.session, callbacks, user_data,
					    option);
		return session;
	}

	static Session NewClient(const nghttp2_session_callbacks *callbacks,
				 void *user_data,
				 const nghttp2_option *option) noexcept {
		Session session;
		nghttp2_session_client_new2(&session.session,
					    callbacks, user_data,
					    option);
		return session;
	}

	auto *get() const noexcept {
		return session;
	}

	gcc_pure
	void *GetStreamUserData(int32_t stream_id) const noexcept {
		return nghttp2_session_get_stream_user_data(session, stream_id);
	}
};

} // namespace NgHttp2
