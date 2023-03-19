// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Parse HTTP headers into a StringMap.
 */

#pragma once

#include <string_view>

class AllocatorPtr;
class StringMap;
class GrowingBuffer;

/**
 * @return true on success, false on error
 */
bool
header_parse_line(AllocatorPtr alloc, StringMap &headers,
		  std::string_view line) noexcept;

void
header_parse_buffer(AllocatorPtr alloc, StringMap &headers,
		    GrowingBuffer &&gb) noexcept;

/**
 * Find the value of the (first) header with the specified name (case
 * sensitive).
 */
[[gnu::pure]]
std::string_view
header_parse_find(std::string_view haystack, std::string_view name) noexcept;
