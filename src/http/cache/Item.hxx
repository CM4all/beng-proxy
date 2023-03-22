// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Document.hxx"
#include "pool/Holder.hxx"
#include "cache.hxx"
#include "memory/Rubber.hxx"
#include "util/IntrusiveHashSet.hxx"

class UnusedIstreamPtr;

class HttpCacheItem final : PoolHolder, public HttpCacheDocument, public CacheItem {
	const char *const tag;

	const size_t size;

	const RubberAllocation body;

public:
	/**
	 * For #HttpCacheHeap::per_tag.
	 */
	IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK> per_tag_hook;

	struct TagHash {
		[[gnu::pure]]
		std::size_t operator()(const char *tag) const noexcept;

		[[gnu::pure]]
		std::size_t operator()(std::string_view tag) const noexcept;

		[[gnu::pure]]
		std::size_t operator()(const HttpCacheItem &item) const noexcept {
			return operator()(item.tag);
		}
	};

	struct TagEqual {
		[[gnu::pure]]
		bool operator()(std::string_view a, std::string_view b) const noexcept {
			return a == b;
		}

		[[gnu::pure]]
		bool operator()(std::string_view a, const HttpCacheItem &b) const noexcept {
			return a == b.tag;
		}
	};

	HttpCacheItem(PoolPtr &&_pool,
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
