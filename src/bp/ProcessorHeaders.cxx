// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ProcessorHeaders.hxx"
#include "strmap.hxx"
#include "AllocatorPtr.hxx"

StringMap
processor_header_forward(struct pool &pool, const StringMap &src)
{
	StringMap dest;

	static const char *const copy_headers[] = {
		"content-language",
		"content-type",
		"content-disposition",
		"location",
		nullptr,
	};

	dest.ListCopyFrom(pool, src, copy_headers);

#ifndef NDEBUG
	/* copy Wildfire headers if present (debug build only, to avoid
	   overhead on production servers) */
	if (src.Get("x-wf-protocol-1") != nullptr)
		dest.PrefixCopyFrom(pool, src, "x-wf-");
#endif

	/* reportedly, the Internet Explorer caches uncacheable resources
	   without revalidating them; only Cache-Control will prevent him
	   from showing stale data to the user */
	dest.Add(pool, "cache-control", "no-store");

	return dest;
}
