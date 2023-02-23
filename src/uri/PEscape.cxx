// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PEscape.hxx"
#include "uri/Escape.hxx"
#include "uri/Unescape.hxx"
#include "AllocatorPtr.hxx"

#include <algorithm>

const char *
uri_escape_dup(AllocatorPtr alloc, std::string_view src,
	       char escape_char)
{
	char *dest = alloc.NewArray<char>(src.size() * 3 + 1);
	size_t dest_length = UriEscape(dest, src, escape_char);
	dest[dest_length] = 0;
	return dest;
}

char *
uri_unescape_dup(AllocatorPtr alloc, std::string_view src,
		 char escape_char)
{
	char *dest = alloc.NewArray<char>(src.size() + 1);
	char *end = UriUnescape(dest, src, escape_char);
	if (end == nullptr)
		return nullptr;

	*end = 0;
	return dest;
}

char *
uri_unescape_concat(AllocatorPtr alloc, std::string_view uri,
		    std::string_view escaped_tail) noexcept
{
	/* worst-case allocation */
	char *dest = alloc.NewArray<char>(uri.size() + escaped_tail.size() + 1);

	/* first copy "uri" */
	char *p = std::copy(uri.begin(), uri.end(), dest);

	/* append "escaped_tail", and fail this function if unescaping
	   fails */
	p = UriUnescape(p, escaped_tail);
	if (p == nullptr)
		return nullptr;

	*p = 0;

	return dest;
}
