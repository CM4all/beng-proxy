// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstddef>
#include <span>

struct pool;
class UnusedIstreamPtr;

/**
 * istream implementation which reads from a fixed memory buffer.
 */
UnusedIstreamPtr
istream_memory_new(struct pool &pool, std::span<const std::byte> src) noexcept;
