// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class AllocatorPtr;
class StringMap;

/**
 * A tag which addresses a resource in the filter cache.
 */
[[gnu::pure]]
const char *
resource_tag_append_etag(AllocatorPtr alloc, const char *tag,
			 const StringMap &headers) noexcept;
