// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

class AllocatorPtr;
class StringMap;

[[gnu::pure]]
const char *
resource_tag_append_filter(AllocatorPtr alloc, std::string_view tag,
			   std::string_view filter_tag) noexcept;

/**
 * A tag which addresses a resource in the filter cache.
 */
[[gnu::pure]]
std::string_view
resource_tag_append_etag_encoding(AllocatorPtr alloc, std::string_view tag,
				  std::string_view etag,
				  std::string_view encoding) noexcept;

/**
 * A tag which addresses a resource in the filter cache.
 */
[[gnu::pure]]
const char *
resource_tag_append_etag(AllocatorPtr alloc, const char *tag,
			 const StringMap &headers) noexcept;
