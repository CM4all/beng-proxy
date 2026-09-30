// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class AllocatorPtr;

/**
 * Normalize an URI path by converting "//" and "/./" to just "/".  It
 * does not handle escaped slashes/dots.  Returns the original pointer
 * if no change is needed.
 *
 * (Unlike uri_compress(), this doesn't resolve "/../")
 */
[[gnu::pure]]
std::string_view
NormalizeUriPath(AllocatorPtr alloc, std::string_view uri) noexcept;
