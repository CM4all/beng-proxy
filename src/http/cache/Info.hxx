// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <chrono>

class AllocatorPtr;

struct HttpCacheRequestInfo {
	const char *if_match, *if_none_match;
	const char *if_modified_since, *if_unmodified_since;

	/**
	 * Is the request served by a remote server?  If yes, then we
	 * require the "Date" header to be present.
	 */
	bool is_remote;

	/**
	 * True if the "Cache-Control" request header contains
	 * "no-cache".
	 *
	 * @see RFC 9111 5.2.1.4
	 */
	bool no_cache;

	/**
	 * True if the "Cache-Control" request header contains
	 * "only-if-cached".
	 *
	 * @see RFC 9111 5.2.1.7
	 */
	bool only_if_cached;

	/** does the request URI have a query string?  This information is
	    important for RFC 2616 13.9 */
	bool has_query_string;
};

struct HttpCacheResponseInfo {
	/** when will the cached resource expire? (beng-proxy time) */
	std::chrono::system_clock::time_point expires;

	/** when was the cached resource last modified on the widget
	    server? (widget server time) */
	const char *last_modified;

	const char *etag;

	const char *vary;

	HttpCacheResponseInfo() = default;
	HttpCacheResponseInfo(AllocatorPtr alloc,
			      const HttpCacheResponseInfo &src) noexcept;

	HttpCacheResponseInfo(HttpCacheResponseInfo &&) = default;
	HttpCacheResponseInfo &operator=(HttpCacheResponseInfo &&) = default;

	void MoveToPool(AllocatorPtr alloc) noexcept;
};
