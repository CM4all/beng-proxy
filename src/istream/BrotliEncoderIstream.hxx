// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;

/**
 * An #Istream filter which compresses data on-the-fly with Brotli.
 */
UnusedIstreamPtr
NewBrotliEncoderIstream(struct pool &pool, UnusedIstreamPtr input) noexcept;
