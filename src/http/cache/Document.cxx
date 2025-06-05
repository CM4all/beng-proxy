// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Document.hxx"
#include "RFC.hxx"
#include "AllocatorPtr.hxx"
#include "http/Status.hxx"

#include <cassert>

HttpCacheDocument::HttpCacheDocument(struct pool &pool,
				     const HttpCacheResponseInfo &_info,
				     const StringMap &request_headers,
				     HttpStatus _status,
				     const StringMap &_response_headers) noexcept
	:info(pool, _info),
	 vary(_info.vary != nullptr
	      ? http_cache_copy_vary(pool, _info.vary, request_headers)
	      : StringMap{}),
	 status(_status),
	 response_headers(pool, _response_headers)
{
	assert(http_status_is_valid(_status));
}

bool
HttpCacheDocument::VaryFits(const StringMap &request_headers) const noexcept
{
	return http_cache_vary_fits(vary, request_headers);
}
