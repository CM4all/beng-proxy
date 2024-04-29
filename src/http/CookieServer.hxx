// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Handle cookies sent by the HTTP client.
 */

#pragma once

#include <string_view>

class AllocatorPtr;

/**
 * Remove cookies with the specified name from a Cookie request
 * header.  Returns the input string if such a cookie was not found,
 * or a newly allocated string.  Returns nullptr when no cookies
 * remain after removing the excluded cookie.
 */
const char *
cookie_exclude(const char *p, const char *exclude,
	       AllocatorPtr alloc) noexcept;
