// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Functions for editing URIs.
 */

#pragma once

#include <string_view>

class AllocatorPtr;

[[gnu::pure]]
const char *
uri_insert_query_string(AllocatorPtr alloc, const char *uri,
			const char *query_string) noexcept;

/**
 * Appends the specified query string at the end.  Adds a '?' or '&'
 * if appropriate.
 */
[[gnu::pure]]
const char *
uri_append_query_string_n(AllocatorPtr alloc, const char *uri,
			  std::string_view query_string) noexcept;

[[gnu::pure]]
const char *
uri_delete_query_string(AllocatorPtr alloc, const char *uri,
			std::string_view needle) noexcept;

[[gnu::pure]]
const char *
uri_insert_args(AllocatorPtr alloc, const char *uri,
		std::string_view args, std::string_view path) noexcept;
