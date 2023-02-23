// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef BENG_PROXY_RESOURCE_TAG_HXX
#define BENG_PROXY_RESOURCE_TAG_HXX

class AllocatorPtr;
class StringMap;

/**
 * A tag which addresses a resource in the filter cache.
 */
const char *
resource_tag_append_etag(AllocatorPtr alloc, const char *tag,
			 const StringMap &headers);

#endif
