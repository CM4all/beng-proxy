// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "resource_tag.hxx"
#include "strmap.hxx"
#include "http/CommonHeaders.hxx"
#include "http/List.hxx"
#include "util/djb_hash.hxx"
#include "util/SpanCast.hxx"
#include "util/StringWithHash.hxx"
#include "AllocatorPtr.hxx"

using std::string_view_literals::operator""sv;

StringWithHash
resource_tag_append_filter(AllocatorPtr alloc, StringWithHash tag,
			   std::string_view filter_tag) noexcept
{
	return StringWithHash{
		alloc.ConcatView(tag.value, '|', filter_tag),
		djb_hash(AsBytes(filter_tag), tag.hash),
	};
}

StringWithHash
resource_tag_append_etag_encoding(AllocatorPtr alloc, StringWithHash tag,
				  std::string_view etag,
				  std::string_view encoding) noexcept
{
	return StringWithHash{
		alloc.ConcatView(tag.value, "|etag="sv, etag, "."sv, encoding),
		djb_hash(AsBytes(encoding), djb_hash(AsBytes(etag), tag.hash)),
	};
}

StringWithHash
resource_tag_append_etag(AllocatorPtr alloc, const StringWithHash tag,
			 const StringMap &headers) noexcept
{
	const char *etag, *p;

	if (tag.IsNull())
		return StringWithHash{nullptr};

	etag = headers.Get(etag_header);
	if (etag == NULL)
		return StringWithHash{nullptr};

	p = headers.Get(cache_control_header);
	if (p != NULL && http_list_contains(p, "no-store"))
		/* generating a resource tag for the cache is pointless,
		   because we are not allowed to store the response anyway */
		return StringWithHash{nullptr};

	const std::string_view etag_v{etag};

	return StringWithHash{
		alloc.ConcatView(tag.value, "|etag="sv, etag_v),
		djb_hash(AsBytes(etag_v), tag.hash),
	};
}

