// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class BareInetAddress;

/**
 * Wrapper for BareInetAddress::Parse() which copies the
 * std::string_view to a null-terminated buffer.
 *
 * Additionally, it strips the IPv6 scope identifier (which
 * BareInetAddress::Parse() would fail upon seeing).
 */
[[nodiscard]]
bool
ParseBareInetAddress(BareInetAddress &address, std::string_view s) noexcept;
