// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Document.hxx"
#include "pool/Holder.hxx"
#include "cache/Item.hxx"
#include "memory/Rubber.hxx"
#include "util/IntrusiveHashSet.hxx"

class UnusedIstreamPtr;

class HttpCacheItem final : PoolHolder, public HttpCacheDocument, public CacheItem {
	const char *const tag;

	const size_t size;

	const RubberAllocation body;

	/**
	 * The number of times the response headers have been updated
	 * by a "304 Not Modified" response.
	 */
	unsigned n_header_updates = 0;

	/**
	 * How many header updates are allowed.  This limits how much
	 * additional memory for new header values can be allocated.
	 */
	static constexpr unsigned MAX_HEADER_UPDATES = 64;

public:
	/**
	 * For #HttpCacheHeap::per_tag.
	 */
	IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK> per_tag_hook;

	struct GetTagFunction {
		[[gnu::pure]]
		std::string_view operator()(const HttpCacheItem &item) const noexcept {
			return item.GetTag();
		}
	};

	HttpCacheItem(PoolPtr &&_pool,
		      StringWithHash _key,
		      std::chrono::steady_clock::time_point now,
		      std::chrono::system_clock::time_point system_now,
		      const char *_tag,
		      const HttpCacheResponseInfo &_info,
		      const StringMap &_request_headers,
		      HttpStatus _status,
		      const StringMap &_response_headers,
		      size_t _size,
		      RubberAllocation &&_body) noexcept;

	HttpCacheItem(const HttpCacheItem &) = delete;
	HttpCacheItem &operator=(const HttpCacheItem &) = delete;

	using PoolHolder::GetPool;

	const char *GetTag() const noexcept {
		return tag;
	}

	/**
	 * May the response headers be updated once more?
	 */
	bool IsHeaderUpdateAllowed() const noexcept {
		return n_header_updates < MAX_HEADER_UPDATES;
	}

	void AccountHeaderUpdate() noexcept {
		++n_header_updates;
	}

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
