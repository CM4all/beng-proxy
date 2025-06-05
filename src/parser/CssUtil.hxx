// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/*
 * CSS utility functions.
 */

#include "CssSyntax.hxx"

#include <algorithm>
#include <string_view>

/**
 * Count the number of leading underscores.  Returns 0 if the
 * underscores are not followed by a different name character.
 */
[[gnu::pure]]
static inline unsigned
underscore_prefix(std::string_view s) noexcept
{
	const char *q = std::find_if(s.begin(), s.end(),
				     [](char ch){ return ch != '_'; });

	return q - s.data();
}

[[gnu::pure]]
static inline bool
is_underscore_prefix(std::string_view s) noexcept
{
	unsigned n = underscore_prefix(s);
	return n == 2 || n == 3;
}
