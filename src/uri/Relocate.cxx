/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "Relocate.hxx"
#include "uri/Extract.hxx"
#include "util/StringView.hxx"
#include "AllocatorPtr.hxx"

/**
 * If the given URI matches the #HttpAddress regarding and port, then
 * return the URI path.  If not, return nullptr.
 */
[[gnu::pure]]
static const char *
MatchUriHost(const char *uri, const char *host) noexcept
{
	const auto h = UriHostAndPort(uri);
	if (h.data() != nullptr) {
		if (host == nullptr)
			/* this is URI_SCHEME_UNIX, and its host cannot be
			   verified */
			return nullptr;

		if (h != host)
			/* host/port mismatch */
			return nullptr;

		uri = h.end();
	}

	if (*uri != '/')
		/* relative URIs are not (yet?) supported here */
		return nullptr;

	return uri;
}

[[gnu::pure]]
static StringView
UriBaseTail(StringView uri, StringView base) noexcept
{
	return uri.StartsWith(base)
		? StringView(uri.data + base.size, uri.end())
		: nullptr;
}

[[gnu::pure]]
static StringView
UriPrefixBeforeTail(StringView uri, StringView tail) noexcept
{
	return uri.size > tail.size &&
		memcmp(uri.end() - tail.size, tail.data, tail.size) == 0 &&
		uri[uri.size - tail.size - 1] == '/'
		? StringView(uri.begin(), uri.end() - tail.size)
		: nullptr;
}

const char *
RelocateUri(AllocatorPtr alloc, const char *uri,
	    const char *internal_host, StringView internal_path,
	    const char *external_scheme, const char *external_host,
	    StringView external_path, StringView base) noexcept
{
	const char *path = MatchUriHost(uri, internal_host);
	if (path == nullptr)
		return nullptr;

	const StringView tail = UriBaseTail(external_path, base);
	if (tail.IsNull())
		return nullptr;

	const StringView prefix = UriPrefixBeforeTail(internal_path, tail);
	if (prefix.IsNull())
		return nullptr;

	const StringView tail2 = UriBaseTail(path, prefix);
	if (tail2.IsNull())
		return nullptr;

	return alloc.Concat(external_scheme, "://",
			    external_host, base, tail2);
}
