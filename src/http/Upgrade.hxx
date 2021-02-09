/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Helpers for implementing HTTP "Upgrade".
 */

#pragma once

#include "http/Status.h"

class StringMap;
class HttpHeaders;

[[gnu::pure]]
static inline bool
http_is_upgrade(http_status_t status) noexcept
{
	return status == HTTP_STATUS_SWITCHING_PROTOCOLS;
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
http_is_upgrade(http_status_t status, const StringMap &headers) noexcept
{
	return http_is_upgrade(status) && http_is_upgrade(headers);
}

[[gnu::pure]]
static inline bool
http_is_upgrade(http_status_t status, const HttpHeaders &headers) noexcept
{
	return http_is_upgrade(status) && http_is_upgrade(headers);
}
