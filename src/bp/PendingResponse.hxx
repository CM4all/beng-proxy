// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/Headers.hxx"
#include "istream/UnusedHoldPtr.hxx"

#include <cstdint>

enum class HttpStatus : uint_least16_t;

struct PendingResponse {
	HttpStatus status;

	HttpHeaders headers;

	UnusedHoldIstreamPtr body;

	template<typename H, typename B>
	PendingResponse(HttpStatus _status, H &&_headers, B &&_body) noexcept
		:status(_status), headers(std::forward<H>(_headers)),
		 body(std::forward<B>(_body)) {}
};
