// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Functions for working with relative URIs.
 */

#pragma once

#include <string_view>

class AllocatorPtr;

/**
 * Compresses an URI (eliminates all "/./" and "/../"), and returns
 * the result.  May return NULL if there are too many "/../".
 */
[[gnu::pure]]
const char *
uri_compress(AllocatorPtr alloc, const char *uri) noexcept;

/**
 * Append a relative URI to an absolute base URI, and return the
 * resulting absolute URI.  Will never return NULL, as there is no
 * error checking.
 */
[[gnu::pure]]
const char *
uri_absolute(AllocatorPtr alloc, const char *base,
	     std::string_view uri) noexcept;
