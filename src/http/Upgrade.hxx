// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Helpers for implementing HTTP "Upgrade".
 */

#pragma once

#include "http/Status.hxx"

class StringMap;
class HttpHeaders;

static constexpr bool
http_is_upgrade(HttpStatus status) noexcept
{
	return status == HttpStatus::SWITCHING_PROTOCOLS;
}

/**
 * Does the "Upgrade" header exist?
 */
[[gnu::pure]]
bool
http_is_upgrade(const StringMap &headers) noexcept;

/**
 * Does the "Upgrade" header exist?
 */
[[gnu::pure]]
bool
http_is_upgrade(const HttpHeaders &headers) noexcept;

[[gnu::pure]]
static inline bool
http_is_upgrade(HttpStatus status, const StringMap &headers) noexcept
{
	return http_is_upgrade(status) && http_is_upgrade(headers);
}

[[gnu::pure]]
static inline bool
http_is_upgrade(HttpStatus status, const HttpHeaders &headers) noexcept
{
	return http_is_upgrade(status) && http_is_upgrade(headers);
}
