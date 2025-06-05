// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <string_view>

class AllocatorPtr;

/**
 * Generate a "https://" redirect URI for the current request.
 *
 * @param host the Host request header
 * @param port the new port; 0 means default
 * @param uri the request URI
 */
std::string_view
MakeHttpsRedirect(AllocatorPtr alloc, const char *host, uint16_t port,
		  const char *uri) noexcept;
