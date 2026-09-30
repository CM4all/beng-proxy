// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstddef>
#include <string_view>

struct StringMapKey;
class StringMap;
class AllocatorPtr;

/**
 * Return the combined header value according to RFC 9110 5.2; empty
 * values are skipped.  Returns a nulled std::string_view if the
 * header does not exist (or if all values are empty).  If only one
 * value exists and #always_copy is false, its pointer is returned
 * as-is without making a copy with the specified #AllocatorPtr.  The
 * pointed-to buffer is null-terminated.
 */
[[gnu::pure]]
std::string_view
GetCombinedHeader(AllocatorPtr alloc, const StringMap &map, StringMapKey key,
		  bool always_copy) noexcept;

/**
 * Does the concatenation of all values of the named header equal the
 * given string?  Repeated header lines are separate #StringMap items,
 * but the origin server sees (and selects on) their combination (RFC
 * 9111 4.1).
 */
[[gnu::pure]]
bool
CombinedHeaderEquals(const StringMap &headers, StringMapKey key,
		     std::string_view expected) noexcept;
