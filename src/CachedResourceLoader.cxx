/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "CachedResourceLoader.hxx"
#include "http_cache.hxx"
#include "istream/UnusedPtr.hxx"

#include <utility>

void
CachedResourceLoader::SendRequest(struct pool &pool,
				  const StopwatchPtr &parent_stopwatch,
				  sticky_hash_t sticky_hash,
				  const char *cache_tag,
				  const char *site_name,
				  http_method_t method,
				  const ResourceAddress &address,
				  gcc_unused http_status_t status,
				  StringMap &&headers,
				  UnusedIstreamPtr body,
				  gcc_unused const char *body_etag,
				  HttpResponseHandler &handler,
				  CancellablePointer &cancel_ptr) noexcept
{
	http_cache_request(cache, pool, parent_stopwatch,
			   sticky_hash, cache_tag, site_name,
			   method, address,
			   std::move(headers), std::move(body),
			   handler, cancel_ptr);
}
