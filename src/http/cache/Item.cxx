// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Item.hxx"
#include "Age.hxx"
#include "memory/istream_rubber.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"

HttpCacheItem::HttpCacheItem(PoolPtr &&_pool,
			     std::chrono::steady_clock::time_point now,
			     std::chrono::system_clock::time_point system_now,
			     const HttpCacheResponseInfo &_info,
			     const StringMap &_request_headers,
			     HttpStatus _status,
			     const StringMap &_response_headers,
			     size_t _size,
			     RubberAllocation &&_body) noexcept
	:PoolHolder(std::move(_pool)),
	 HttpCacheDocument(pool, _info, _request_headers,
			   _status, _response_headers),
	 CacheItem(http_cache_calc_expires(now, system_now, _info.expires, vary),
		   pool_netto_size(pool) + _size),
	 size(_size),
	 body(std::move(_body))
{
}

void
HttpCacheItem::SetExpires(std::chrono::steady_clock::time_point steady_now,
			  std::chrono::system_clock::time_point system_now,
			  std::chrono::system_clock::time_point _expires) noexcept
{
	info.expires = _expires;
	CacheItem::SetExpires(http_cache_calc_expires(steady_now, system_now,
						      _expires, vary));
}

UnusedIstreamPtr
HttpCacheItem::OpenStream(struct pool &_pool) noexcept
{
	return istream_rubber_new(_pool, body.GetRubber(), body.GetId(),
				  0, size, false);
}

void
HttpCacheItem::Destroy() noexcept
{
	pool_trash(pool);
	this->~HttpCacheItem();
}
