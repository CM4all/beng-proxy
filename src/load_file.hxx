// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstddef>
#include <span>

#include <sys/types.h>

struct pool;

/**
 * Load the contents of a file into a buffer.
 *
 * Throws exception on error.
 */
std::span<const std::byte>
LoadFile(struct pool &pool, const char *path, off_t max_size);
