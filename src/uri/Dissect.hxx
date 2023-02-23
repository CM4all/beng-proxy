// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

/**
 * A splitted URI.
 */
struct DissectedUri {
	/**
	 * The "base" URI that points to the real resource, without
	 * dynamic arguments.
	 */
	std::string_view base;

	/**
	 * The beng-proxy arguments, which were introduced by a semicolon
	 * (without the semicolon).
	 */
	std::string_view args;

	/**
	 * The URI portion after the arguments, including the leading
	 * slash.
	 */
	std::string_view path_info;

	/**
	 * The query string (without the question mark).
	 */
	std::string_view query;

	/**
	 * Split the URI into its parts.  The result contains pointers
	 * into the original string.
	 */
	bool Parse(std::string_view src) noexcept;
};
