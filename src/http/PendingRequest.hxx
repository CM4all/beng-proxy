// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "istream/UnusedHoldPtr.hxx"
#include "strmap.hxx"

#include <cstdint>

enum class HttpMethod : uint_least8_t;

struct PendingHttpRequest {
	HttpMethod method;

	const char *uri;

	StringMap headers;

	UnusedHoldIstreamPtr body;

	template<typename H, typename B>
	PendingHttpRequest(struct pool &pool,
			   HttpMethod _method, const char *_uri,
			   H &&_headers, B &&_body) noexcept
		:method(_method), uri(_uri),
		 headers(std::forward<H>(_headers)),
		 body(pool, std::forward<B>(_body)) {}

	void Discard() noexcept {
		body.Clear();
	}
};
