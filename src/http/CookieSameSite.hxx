// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <string_view>

enum class CookieSameSite : uint8_t {
	DEFAULT,
	STRICT,
	LAX,
	NONE,
};

/**
 * Throws on error.
 */
CookieSameSite
ParseCookieSameSite(std::string_view s);
