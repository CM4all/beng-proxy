// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class AllocatorPtr;
class StringMap;
struct StringWithHash;

[[gnu::pure]]
StringWithHash
resource_tag_concat(AllocatorPtr alloc, StringWithHash a,
		    std::string_view separator, StringWithHash b) noexcept;

[[gnu::pure]]
StringWithHash
resource_tag_append_filter(AllocatorPtr alloc, StringWithHash tag,
			   StringWithHash filter_tag) noexcept;

/**
 * A tag which addresses a resource in the filter cache.
 */
[[gnu::pure]]
StringWithHash
resource_tag_append_etag_encoding(AllocatorPtr alloc, StringWithHash tag,
				  std::string_view etag,
				  std::string_view encoding) noexcept;

/**
 * A tag which addresses a resource in the filter cache.
 */
[[gnu::pure]]
StringWithHash
resource_tag_append_etag(AllocatorPtr alloc, StringWithHash tag,
			 const StringMap &headers) noexcept;
