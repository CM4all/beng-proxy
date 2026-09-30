// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PNormalize.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringCompare.hxx"

#include <cassert>

#include <string.h>

using std::string_view_literals::operator""sv;

std::string_view
NormalizeUriPath(AllocatorPtr alloc, std::string_view uri) noexcept
{
	while (uri.starts_with("./"sv))
		uri.remove_prefix(2);

	if (uri == "."sv)
		return "";

	if (!uri.contains("//"sv) &&
	    !uri.contains("/./"sv) &&
	    !uri.ends_with("/."sv))
		/* cheap route: the URI is already compressed, do not
		   duplicate anything */
		return uri;

	char *dest = alloc.NewArray<char>(uri.size() + 1);
	*std::copy(uri.begin(), uri.end(), dest) = '\0';

	/* eliminate "//" */

	char *p;
	while ((p = strstr(dest, "//")) != nullptr)
		/* strcpy() might be better here, but it does not allow
		   overlapped arguments */
		memmove(p + 1, p + 2, strlen(p + 2) + 1);

	/* eliminate "/./" */

	while ((p = strstr(dest, "/./")) != nullptr)
		/* strcpy() might be better here, but it does not allow
		   overlapped arguments */
		memmove(p + 1, p + 3, strlen(p + 3) + 1);

	/* eliminate trailing "/." */

	p = strrchr(dest, '/');
	if (p != nullptr) {
		if (p[1] == '.' && p[2] == 0)
			p[1] = 0;
	}

	if (dest[0] == '.' && dest[1] == 0) {
		/* if the string doesn't start with a slash, then an empty
		   return value is allowed */
		return "";
	}

	return dest;
}
