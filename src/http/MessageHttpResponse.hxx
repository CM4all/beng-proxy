// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>

enum class HttpStatus : uint_least16_t;


/**
 * Describes a very simple HTTP response with a text/plain body.
 */
struct MessageHttpResponse {
	HttpStatus status;

	/**
	 * The response body.  This string must either be a literal or the
	 * entity which constructs this object must ensure that it will be
	 * valid until sending the response has finished (e.g. by
	 * allocating on the #IncomingHttpRequest pool).
	 */
	const char *message;
};
