// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class SharedLease;

/**
 * An istream facade which releases a #SharedLease after it has been
 * closed.
 */
UnusedIstreamPtr
NewSharedLeaseIstream(struct pool &pool, UnusedIstreamPtr &&input,
		      SharedLease &&lease) noexcept;
