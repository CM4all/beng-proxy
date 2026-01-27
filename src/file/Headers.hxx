// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Handle the request/response headers for static files.
 */

#pragma once

struct pool;
class StringMap;
struct statx;

void
GetAnyETag(char *buffer, const struct statx &st) noexcept;

StringMap
static_response_headers(struct pool &pool,
			const struct statx &st,
			const char *content_type) noexcept;
