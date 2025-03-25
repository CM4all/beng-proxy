// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "istream.hxx"
#include "Bucket.hxx"

#include <utility> // for std::unreachable()

Istream::~Istream() noexcept
{
#ifndef NDEBUG
	assert(!destroyed);
	destroyed = true;
#endif
}

void
Istream::_FillBucketList(IstreamBucketList &list)
{
	list.EnableFallback();
}

[[gnu::noreturn]]
Istream::ConsumeBucketResult
Istream::_ConsumeBucketList(std::size_t) noexcept
{
	std::unreachable();
}

[[gnu::noreturn]]
void
Istream::_ConsumeDirect(std::size_t) noexcept
{
	std::unreachable();
}
