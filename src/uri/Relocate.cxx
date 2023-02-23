// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Relocate.hxx"
#include "uri/Extract.hxx"
#include "util/StringCompare.hxx"
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
static std::string_view
UriBaseTail(std::string_view uri, std::string_view base) noexcept
{
	return SkipPrefix(uri, base)
		? uri
		: std::string_view{};
}

[[gnu::pure]]
static std::string_view
UriPrefixBeforeTail(std::string_view uri, std::string_view tail) noexcept
{
	return RemoveSuffix(uri, tail) && uri.ends_with('/')
		? uri
		: std::string_view{};
}

const char *
RelocateUri(AllocatorPtr alloc, const char *uri,
	    const char *internal_host, std::string_view internal_path,
	    const char *external_scheme, const char *external_host,
	    std::string_view external_path, std::string_view base) noexcept
{
	const char *path = MatchUriHost(uri, internal_host);
	if (path == nullptr)
		return nullptr;

	const std::string_view tail = UriBaseTail(external_path, base);
	if (tail.data() == nullptr)
		return nullptr;

	const std::string_view prefix = UriPrefixBeforeTail(internal_path, tail);
	if (prefix.data() == nullptr)
		return nullptr;

	const std::string_view tail2 = UriBaseTail(path, prefix);
	if (tail2.data() == nullptr)
		return nullptr;

	return alloc.Concat(external_scheme, "://",
			    external_host, base, tail2);
}
