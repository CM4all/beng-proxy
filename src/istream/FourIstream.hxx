// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;

/**
 * This istream filter passes no more than four bytes at a time.  This
 * is useful for testing and debugging istream handler
 * implementations.
 */
UnusedIstreamPtr
istream_four_new(struct pool *pool, UnusedIstreamPtr input) noexcept;
