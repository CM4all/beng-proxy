// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Various utilities for working with HTTP objects.
 */

#pragma once

#include <chrono>
#include <string_view>

class StringMap;

[[gnu::pure]]
int
http_client_accepts_encoding(const StringMap &request_headers,
			     std::string_view coding) noexcept;

/**
 * Parse the "Date" response header.
 *
 * @return time_t(-1) if there is no "Date" header or if it could not
 * be parsed
 */
[[gnu::pure]]
std::chrono::system_clock::time_point
GetServerDate(const StringMap &response_headers) noexcept;
