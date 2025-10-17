// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class AllocatorPtr;
struct escape_class;

[[gnu::pure]]
std::string_view
escape_dup(AllocatorPtr alloc, const struct escape_class &cls,
	   std::string_view p) noexcept;

/**
 * Like escape_dup(), but return the original input string (without
 * copying it) if nothing needs to be escaped.
 */
[[gnu::pure]]
std::string_view
optional_escape_dup(AllocatorPtr alloc, const struct escape_class &cls,
		    std::string_view p) noexcept;

[[gnu::pure]]
std::string_view
unescape_dup(AllocatorPtr alloc, const struct escape_class &cls,
	     std::string_view src) noexcept;
