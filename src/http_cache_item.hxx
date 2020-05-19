/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "http_cache_document.hxx"
#include "pool/Holder.hxx"
#include "cache.hxx"
#include "rubber.hxx"

class UnusedIstreamPtr;

struct HttpCacheItem final : PoolHolder, HttpCacheDocument, CacheItem {
	size_t size;

	const RubberAllocation body;

	HttpCacheItem(PoolPtr &&_pool,
		      std::chrono::steady_clock::time_point now,
		      std::chrono::system_clock::time_point system_now,
		      const HttpCacheResponseInfo &_info,
		      const StringMap &_request_headers,
		      http_status_t _status,
		      const StringMap &_response_headers,
		      size_t _size,
		      RubberAllocation &&_body) noexcept;

	HttpCacheItem(const HttpCacheItem &) = delete;
	HttpCacheItem &operator=(const HttpCacheItem &) = delete;

	using PoolHolder::GetPool;

	UnusedIstreamPtr OpenStream(struct pool &_pool) noexcept;

	/* virtual methods from class CacheItem */
	void Destroy() noexcept override {
		pool_trash(pool);
		this->~HttpCacheItem();
	}
};
