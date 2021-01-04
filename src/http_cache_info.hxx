/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <chrono>

struct HttpCacheRequestInfo {
	/**
	 * Is the request served by a remote server?  If yes, then we
	 * require the "Date" header to be present.
	 */
	bool is_remote;

	bool only_if_cached = false;

	/** does the request URI have a query string?  This information is
	    important for RFC 2616 13.9 */
	bool has_query_string;

	const char *if_match, *if_none_match;
	const char *if_modified_since, *if_unmodified_since;
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
	HttpCacheResponseInfo(struct pool &pool,
			      const HttpCacheResponseInfo &src) noexcept;

	HttpCacheResponseInfo(const HttpCacheResponseInfo &) = delete;
	HttpCacheResponseInfo &operator=(const HttpCacheResponseInfo &) = delete;

	void MoveToPool(struct pool &pool) noexcept;
};
