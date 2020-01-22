/*
 * Copyright 2007-2019 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
