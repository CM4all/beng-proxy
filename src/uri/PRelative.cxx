// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PRelative.hxx"
#include "uri/Extract.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <string.h>

using std::string_view_literals::operator""sv;

const char *
uri_compress(AllocatorPtr alloc, const char *uri) noexcept
{
	assert(uri != nullptr);

	while (uri[0] == '.' && uri[1] == '/')
		uri += 2;

	if (uri[0] == '.' && uri[1] == '.' &&
	    (uri[2] == '/' || uri[2] == 0))
		return nullptr;

	if (strstr(uri, "//") == nullptr &&
	    strstr(uri, "/./") == nullptr &&
	    strstr(uri, "/..") == nullptr)
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

	/* eliminate "/../" with backtracking */

	while ((p = strstr(dest, "/../")) != nullptr) {
		if (p == dest) {
			/* this ".." cannot be resolved - scream! */
			return nullptr;
		}

		char *q = p;

		/* backtrack to the previous slash - we can't use strrchr()
		   here, and memrchr() is not portable :( */

		do {
			--q;
		} while (q >= dest && *q != '/');

		/* kill it */

		memmove(q + 1, p + 4, strlen(p + 4) + 1);
	}

	/* eliminate trailing "/." and "/.." */

	p = strrchr(dest, '/');
	if (p != nullptr) {
		if (p[1] == '.' && p[2] == 0)
			p[1] = 0;
		else if (p[1] == '.' && p[2] == '.' && p[3] == 0) {
			if (p == dest) {
				/* refuse to delete the leading slash */
				return nullptr;
			}

			*p = 0;

			p = strrchr(dest, '/');
			if (p == nullptr) {
				/* if the string doesn't start with a slash, then an
				   empty return value is allowed */
				return "";
			}

			p[1] = 0;
		}
	}

	if (dest[0] == '.' && dest[1] == 0) {
		/* if the string doesn't start with a slash, then an empty
		   return value is allowed */
		return "";
	}

	return dest;
}

static const char *
uri_after_last_slash(const char *uri) noexcept
{
	const char *path = UriPathQueryFragment(uri);
	if (path == nullptr)
		return nullptr;

	uri = strrchr(path, '/');
	if (uri != nullptr)
		++uri;
	return uri;
}

const char *
uri_absolute(AllocatorPtr alloc, const char *base,
	     std::string_view uri) noexcept
{
	assert(base != nullptr);

	if (uri.empty())
		return base;

	if (UriHasScheme(uri))
		return alloc.DupZ(uri);

	size_t base_length;
	if (uri.starts_with("//"sv)) {
		const char *colon = strstr(base, "://");
		if (colon != nullptr)
			base_length = colon + 1 - base;
		else
			base_length = 0;
	} else if (uri[0] == '/') {
		if (base[0] == '/' && base[1] != '/')
			return alloc.DupZ(uri);

		const char *base_path = UriPathQueryFragment(base);
		if (base_path == nullptr)
			return alloc.Concat(base, uri);

		base_length = base_path - base;
	} else if (uri[0] == '?') {
		const char *qmark = strchr(base, '?');
		base_length = qmark != nullptr ? (size_t)(qmark - base) : strlen(base);
	} else {
		const char *base_end = uri_after_last_slash(base);
		if (base_end == nullptr)
			return alloc.Concat(base, "/", uri);

		base_length = base_end - base;
	}

	return alloc.Concat(std::string_view{base, base_length}, uri);
}
