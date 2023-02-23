// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Document.hxx"
#include "RFC.hxx"
#include "AllocatorPtr.hxx"
#include "http/Status.hxx"

HttpCacheDocument::HttpCacheDocument(struct pool &pool,
				     const HttpCacheResponseInfo &_info,
				     const StringMap &request_headers,
				     HttpStatus _status,
				     const StringMap &_response_headers) noexcept
	:info(pool, _info),
	 status(_status),
	 response_headers(pool, _response_headers)
{
	assert(http_status_is_valid(_status));

	if (_info.vary != nullptr)
		http_cache_copy_vary(vary, pool, _info.vary, request_headers);
}

bool
HttpCacheDocument::VaryFits(const StringMap &request_headers) const noexcept
{
	return http_cache_vary_fits(vary, request_headers);
}
