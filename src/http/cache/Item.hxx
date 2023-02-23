// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Document.hxx"
#include "pool/Holder.hxx"
#include "cache.hxx"
#include "memory/Rubber.hxx"
#include "util/IntrusiveList.hxx"

class UnusedIstreamPtr;

class HttpCacheItem final : PoolHolder, public HttpCacheDocument, public CacheItem {
	const size_t size;

	const RubberAllocation body;

public:
	/**
	 * A doubly linked list of cache items with the same cache tag.
	 */
	IntrusiveListHook<IntrusiveHookMode::AUTO_UNLINK> per_tag_siblings;

	HttpCacheItem(PoolPtr &&_pool,
		      std::chrono::steady_clock::time_point now,
		      std::chrono::system_clock::time_point system_now,
		      const HttpCacheResponseInfo &_info,
		      const StringMap &_request_headers,
		      HttpStatus _status,
		      const StringMap &_response_headers,
		      size_t _size,
		      RubberAllocation &&_body) noexcept;

	HttpCacheItem(const HttpCacheItem &) = delete;
	HttpCacheItem &operator=(const HttpCacheItem &) = delete;

	using PoolHolder::GetPool;

	void SetExpires(std::chrono::steady_clock::time_point steady_now,
			std::chrono::system_clock::time_point system_now,
			std::chrono::system_clock::time_point _expires) noexcept;

	bool HasBody() const noexcept {
		return body;
	}

	UnusedIstreamPtr OpenStream(struct pool &_pool) noexcept;

	/* virtual methods from class CacheItem */
	void Destroy() noexcept override;
};
