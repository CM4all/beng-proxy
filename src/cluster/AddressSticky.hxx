// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "StickyHash.hxx"

class SocketAddress;

[[gnu::pure]]
sticky_hash_t
socket_address_sticky(SocketAddress address) noexcept;
