// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/Status.hxx"

#include <utility>
#include <exception>

#include <assert.h>

struct pool;
class StringMap;
class UnusedIstreamPtr;

/**
 * Definition of the HTTP response handler.
 */
class HttpResponseHandler {
protected:
	virtual void OnHttpResponse(HttpStatus status, StringMap &&headers,
				    UnusedIstreamPtr body) noexcept = 0;

	virtual void OnHttpError(std::exception_ptr ep) noexcept = 0;

public:
	template<typename B>
	void InvokeResponse(HttpStatus status, StringMap &&headers,
			    B &&body) noexcept {
		assert(http_status_is_valid(status));
		assert(!http_status_is_empty(status) || !body);

		OnHttpResponse(status, std::move(headers), std::forward<B>(body));
	}

	/**
	 * Sends a plain-text message.
	 */
	void InvokeResponse(struct pool &pool,
			    HttpStatus status, const char *msg) noexcept;

	void InvokeError(std::exception_ptr ep) noexcept {
		assert(ep);

		OnHttpError(ep);
	}
};
