// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PNormalize.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringCompare.hxx"

#include <cassert>

#include <string.h>

const char *
NormalizeUriPath(AllocatorPtr alloc, const char *uri) noexcept
{
	assert(uri != nullptr);

	while (uri[0] == '.' && uri[1] == '/')
		uri += 2;

	if (StringIsEqual(uri, "."))
		return "";

	if (strstr(uri, "//") == nullptr &&
	    strstr(uri, "/./") == nullptr &&
	    !StringEndsWith(uri, "/."))
		/* cheap route: the URI is already compressed, do not
		   duplicate anything */
		return uri;

	char *dest = alloc.Dup(uri);

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
