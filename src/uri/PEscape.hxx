// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Escape and unescape in URI style ('%20').
 */

#pragma once

#include <string_view>

class AllocatorPtr;

[[gnu::pure]]
const char *
uri_escape_dup(AllocatorPtr alloc, std::string_view src,
	       char escape_char='%');

/**
 * @return nullptr on error
 */
char *
uri_unescape_dup(AllocatorPtr alloc, std::string_view src,
		 char escape_char='%');

/**
 * Concatenate an existing (unescaped) URI and an escaped URI fragment
 * (escaped, to be unescaped by this function).
 *
 * @return nullptr on error
 */
char *
uri_unescape_concat(AllocatorPtr alloc, std::string_view uri,
		    std::string_view escaped_tail) noexcept;
