// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ForwardIstream.hxx"
#include "util/DestructObserver.hxx"

/**
 * This class is an adapter for an #Istream which converts buckets
 * obtained via FillBucketList() to old-style OnData() calls.  which
 * guarantees that FillBucketList() is available.  This allows new
 * #Istream implementations to omit those methods.
 */
class FromBucketIstream final : public ForwardIstream, DestructAnchor {
public:
	FromBucketIstream(struct pool &_pool, UnusedIstreamPtr &&_input) noexcept;

protected:
	/* virtual methods from class Istream */

	void _Read() noexcept override;
	void _Close() noexcept override;
};
