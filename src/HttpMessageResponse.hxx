// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <stdexcept>

enum class HttpStatus : uint_least16_t;

/**
 * An exception which can be thrown to indicate that a certain HTTP
 * response shall be sent to our HTTP client.
 */
class HttpMessageResponse : public std::runtime_error {
	HttpStatus status;

public:
	HttpMessageResponse(HttpStatus _status, const char *_msg) noexcept
		:std::runtime_error(_msg), status(_status) {}

	HttpStatus GetStatus() const noexcept {
		return status;
	}
};
