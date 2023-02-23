// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;

/**
 * This istream filter adds HTTP chunking.
 */
UnusedIstreamPtr
istream_chunked_new(struct pool &pool, UnusedIstreamPtr input) noexcept;
