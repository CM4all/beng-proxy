// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <nghttp2/nghttp2.h>

#include <utility>

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

	[[gnu::pure]]
	void *GetStreamUserData(int32_t stream_id) const noexcept {
		return nghttp2_session_get_stream_user_data(session, stream_id);
	}
};

} // namespace NgHttp2
