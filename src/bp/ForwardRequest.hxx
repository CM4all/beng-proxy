// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Common request forwarding code for the request handlers.
 */

#pragma once

#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"

#include <cstdint>

enum class HttpMethod : uint_least8_t;

struct ForwardRequest {
	HttpMethod method;

	StringMap headers;

	UnusedIstreamPtr body;

	ForwardRequest(HttpMethod _method, StringMap &&_headers,
		       UnusedIstreamPtr _body) noexcept
		:method(_method), headers(std::move(_headers)),
		 body(std::move(_body)) {}
};
