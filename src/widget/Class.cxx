// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Class.hxx"
#include "AllocatorPtr.hxx"

WidgetClass::WidgetClass(AllocatorPtr alloc, const WidgetClass &src) noexcept
	:local_uri(alloc.CheckDup(src.local_uri)),
	 untrusted_host(alloc.CheckDup(src.untrusted_host)),
	 untrusted_prefix(alloc.CheckDup(src.untrusted_prefix)),
	 untrusted_site_suffix(alloc.CheckDup(src.untrusted_site_suffix)),
	 untrusted_raw_site_suffix(alloc.CheckDup(src.untrusted_raw_site_suffix)),
	 cookie_host(alloc.CheckDup(src.cookie_host)),
	 group(alloc.CheckDup(src.group)),
	 direct_addressing(src.direct_addressing),
	 stateful(src.stateful),
	 require_csrf_token(src.require_csrf_token),
	 anchor_absolute(src.anchor_absolute),
	 info_headers(src.info_headers),
	 dump_headers(src.dump_headers)
{
	views.CopyChainFrom(alloc, src.views);
	container_groups.CopyFrom(alloc, src.container_groups);
}
