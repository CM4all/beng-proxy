// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "AllocatorPtr.hxx"
#include "net/SocketAddress.hxx"

/**
 * Allocating struct SocketAddress from memory pool.
 */
static inline SocketAddress
DupAddress(AllocatorPtr alloc, SocketAddress src) noexcept
{
	return src.IsNull()
		? src
		: SocketAddress(alloc.Dup(std::span<const std::byte>(src)));
}
