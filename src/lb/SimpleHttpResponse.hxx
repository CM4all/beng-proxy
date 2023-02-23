// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <string>

enum class HttpStatus : uint_least16_t;
struct IncomingHttpRequest;

struct LbSimpleHttpResponse {
	HttpStatus status = {};

	/**
	 * The "Location" response header.
	 */
	std::string location;

	std::string message;

	bool redirect_https = false;

	LbSimpleHttpResponse() = default;
	explicit LbSimpleHttpResponse(HttpStatus _status) noexcept
		:status(_status) {}

	bool IsDefined() const noexcept {
		return status != HttpStatus{};
	}
};

void
SendResponse(IncomingHttpRequest &request,
	     const LbSimpleHttpResponse &response) noexcept;
