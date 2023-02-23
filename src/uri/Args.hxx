// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

class AllocatorPtr;
class StringMap;

/**
 * Parse the argument list in an URI after the semicolon.
 */
[[gnu::pure]]
StringMap
args_parse(AllocatorPtr alloc, std::string_view p) noexcept;

/**
 * Format the arguments into a string in the form
 * "KEY=VALUE&KEY2=VALUE2&...".
 *
 * @param replace_key add, replace or remove an entry in the args map
 * @param replace_value the new value or nullptr if the key should be removed
 */
[[gnu::pure]]
const char *
args_format_n(AllocatorPtr alloc, const StringMap *args,
	      const char *replace_key, std::string_view replace_value,
	      const char *replace_key2, std::string_view replace_value2,
	      const char *replace_key3, std::string_view replace_value3,
	      const char *remove_key) noexcept;

[[gnu::pure]]
const char *
args_format(AllocatorPtr alloc, const StringMap *args,
	    const char *replace_key, std::string_view replace_value,
	    const char *replace_key2, std::string_view replace_value2,
	    const char *remove_key) noexcept;
