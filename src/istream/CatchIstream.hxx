// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

struct pool;
class UnusedIstreamPtr;

/**
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 */
UnusedIstreamPtr
NewCatchIstream(struct pool *pool, UnusedIstreamPtr input,
		std::exception_ptr (*callback)(std::exception_ptr ep, void *ctx), void *ctx) noexcept;
