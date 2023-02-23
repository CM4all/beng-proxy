// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <stdint.h>

class AllocatorPtr;

/**
 * Generate a "https://" redirect URI for the current request.
 *
 * @param host the Host request header
 * @param port the new port; 0 means default
 * @param uri the request URI
 */
const char *
MakeHttpsRedirect(AllocatorPtr alloc, const char *host, uint16_t port,
		  const char *uri) noexcept;
