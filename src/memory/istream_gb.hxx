// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

struct pool;
class UnusedIstreamPtr;
class GrowingBuffer;

/**
 * A wrapper that turns a #GrowingBuffer into an #Istream.
 */
UnusedIstreamPtr
istream_gb_new(struct pool &pool, GrowingBuffer &&gb) noexcept;
