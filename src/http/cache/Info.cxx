// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Info.hxx"
#include "AllocatorPtr.hxx"

HttpCacheResponseInfo::HttpCacheResponseInfo(AllocatorPtr alloc,
					     const HttpCacheResponseInfo &src) noexcept
	:expires(src.expires),
	 last_modified(alloc.CheckDup(src.last_modified)),
	 etag(alloc.CheckDup(src.etag)),
	 vary(alloc.CheckDup(src.vary))
{
}

void
HttpCacheResponseInfo::MoveToPool(AllocatorPtr alloc) noexcept
{
	last_modified = alloc.CheckDup(last_modified);
	etag = alloc.CheckDup(etag);
	vary = alloc.CheckDup(vary);
}
