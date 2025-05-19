// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

void
FlushInterfaceNameCache() noexcept;

/**
 * Look up the name of the specified interface.  The name is managed
 * in a global cache.
 *
 * This function is not thread-safe.
 *
 * @return a string_view pointing to memory owned by the cache
 * (invalidated by FlushInterfaceNameCache()) or an empty
 * std::string_view on error
 */
[[gnu::pure]]
std::string_view
GetCachedInterfaceName(unsigned index) noexcept;
