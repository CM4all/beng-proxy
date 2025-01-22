// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "resource_tag.hxx"
#include "strmap.hxx"
#include "http/CommonHeaders.hxx"
#include "http/List.hxx"
#include "AllocatorPtr.hxx"

const char *
resource_tag_append_etag(AllocatorPtr alloc, const char *tag,
			 const StringMap &headers) noexcept
{
	const char *etag, *p;

	if (tag == nullptr)
		return NULL;

	etag = headers.Get(etag_header);
	if (etag == NULL)
		return NULL;

	p = headers.Get(cache_control_header);
	if (p != NULL && http_list_contains(p, "no-store"))
		/* generating a resource tag for the cache is pointless,
		   because we are not allowed to store the response anyway */
		return NULL;

	return alloc.Concat(tag, "|etag=", etag);
}

