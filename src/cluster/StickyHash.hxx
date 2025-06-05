// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>

/**
 * A type which can store a hash for choosing a cluster member.  Zero
 * is a special value for "sticky disabled"
 */
typedef uint32_t sticky_hash_t;
