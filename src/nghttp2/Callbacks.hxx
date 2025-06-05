// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <nghttp2/nghttp2.h>

namespace NgHttp2 {

class SessionCallbacks {
	nghttp2_session_callbacks *callbacks;

public:
	SessionCallbacks() noexcept {
		nghttp2_session_callbacks_new(&callbacks);
	}

	~SessionCallbacks() noexcept {
		nghttp2_session_callbacks_del(callbacks);
	}

	SessionCallbacks(const SessionCallbacks &) = delete;
	SessionCallbacks &operator=(const SessionCallbacks &) = delete;

	auto *get() const noexcept {
		return callbacks;
	}
};

} // namespace NgHttp2
