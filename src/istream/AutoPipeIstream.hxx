// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class PipeStock;

/**
 * Convert any file descriptor to a pipe by splicing.
 */
UnusedIstreamPtr
NewAutoPipeIstream(struct pool *pool, UnusedIstreamPtr input,
		   PipeStock *pipe_stock) noexcept;
