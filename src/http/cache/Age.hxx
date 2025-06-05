// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <chrono>

class StringMap;

/**
 * Calculate the "expires" value for the new cache item, based on the
 * "Expires" response header.
 */
[[gnu::pure]]
std::chrono::steady_clock::time_point
http_cache_calc_expires(std::chrono::steady_clock::time_point steady_now,
			std::chrono::system_clock::time_point system_now,
			std::chrono::system_clock::time_point expires,
			const StringMap &vary) noexcept;
