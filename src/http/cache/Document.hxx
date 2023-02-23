// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Info.hxx"
#include "strmap.hxx"

enum class HttpStatus : uint_least16_t;

struct HttpCacheDocument {
	HttpCacheResponseInfo info;

	StringMap vary;

	HttpStatus status;
	StringMap response_headers;

	HttpCacheDocument() = default;

	HttpCacheDocument(struct pool &pool,
			  const HttpCacheResponseInfo &_info,
			  const StringMap &request_headers,
			  HttpStatus _status,
			  const StringMap &response_headers) noexcept;

	HttpCacheDocument(const HttpCacheDocument &) = delete;
	HttpCacheDocument &operator=(const HttpCacheDocument &) = delete;

	/**
	 * Checks whether the specified cache item fits the current request.
	 * This is not true if the Vary headers mismatch.
	 */
	[[gnu::pure]]
	bool VaryFits(const StringMap &request_headers) const noexcept;
};
