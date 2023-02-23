// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <stddef.h>

struct pool;
class UnusedIstreamPtr;

/**
 * This istream filter passes only the first N bytes.
 *
 * @param authoritative is the specified size authoritative?
 */
UnusedIstreamPtr
istream_head_new(struct pool &pool, UnusedIstreamPtr input,
		 size_t size, bool authoritative) noexcept;
