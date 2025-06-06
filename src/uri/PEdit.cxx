// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PEdit.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringCompare.hxx"

#include <assert.h>
#include <string.h>

const char *
uri_insert_query_string(AllocatorPtr alloc, const char *uri,
			const char *query_string) noexcept
{
	assert(uri != nullptr);
	assert(query_string != nullptr);

	const char *qmark = strchr(uri, '?');

	if (qmark != nullptr) {
		++qmark;
		return alloc.Concat(std::string_view(uri, qmark),
				    query_string,
				    '&',
				    qmark);
	} else
		return alloc.Concat(uri, '?', query_string);
}

const char *
uri_append_query_string_n(AllocatorPtr alloc, const char *uri,
			  std::string_view query_string) noexcept
{
	assert(uri != nullptr);

	return alloc.Concat(uri,
			    strchr(uri, '?') == nullptr ? '?' : '&',
			    query_string);
}

static size_t
query_string_begins_with(const char *query_string,
			 std::string_view needle) noexcept
{
	assert(query_string != nullptr);

	query_string = StringAfterPrefix(query_string, needle);
	if (query_string == nullptr)
		return 0;

	if (*query_string == '&')
		return needle.size() + 1;
	else if (*query_string == 0)
		return needle.size();
	else
		return 0;
}

const char *
uri_delete_query_string(AllocatorPtr alloc, const char *uri,
			std::string_view needle) noexcept
{
	assert(uri != nullptr);

	const char *p = strchr(uri, '?');
	if (p == nullptr)
		/* no query string, nothing to remove */
		return uri;

	++p;
	size_t delete_length = query_string_begins_with(p, needle);
	if (delete_length == 0)
		/* mismatch, return original URI */
		return uri;

	if (p[delete_length] == 0) {
		/* empty query string - also delete the question mark */
		--p;
		++delete_length;
	}

	return alloc.Concat(std::string_view(uri, p),
			    std::string_view(p + delete_length));
}

const char *
uri_insert_args(AllocatorPtr alloc, const char *uri,
		std::string_view args, std::string_view path) noexcept
{
	const char *q = strchr(uri, '?');
	if (q == nullptr)
		q = uri + strlen(uri);

	return alloc.Concat(std::string_view(uri, q),
			    ';', args, path, q);
}
