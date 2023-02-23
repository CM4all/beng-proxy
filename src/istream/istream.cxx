// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "istream.hxx"
#include "Bucket.hxx"
#include "util/Compiler.h"

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
	list.SetMore();
}

gcc_noreturn
std::size_t
Istream::_ConsumeBucketList(std::size_t) noexcept
{
	assert(false);
	gcc_unreachable();
}

gcc_noreturn
void
Istream::_ConsumeDirect(std::size_t) noexcept
{
	assert(false);
	gcc_unreachable();
}
