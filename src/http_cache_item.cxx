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

#include "http_cache_item.hxx"
#include "http_cache_age.hxx"
#include "istream_rubber.hxx"
#include "istream/UnusedPtr.hxx"

HttpCacheItem::HttpCacheItem(PoolPtr &&_pool,
			     std::chrono::steady_clock::time_point now,
			     std::chrono::system_clock::time_point system_now,
			     const HttpCacheResponseInfo &_info,
			     const StringMap &_request_headers,
			     http_status_t _status,
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
