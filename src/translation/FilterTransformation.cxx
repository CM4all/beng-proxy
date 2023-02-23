// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FilterTransformation.hxx"
#include "AllocatorPtr.hxx"

FilterTransformation::FilterTransformation(AllocatorPtr alloc,
					   const FilterTransformation &src) noexcept
	:cache_tag(alloc.CheckDup(src.cache_tag)),
	 address(alloc, src.address),
	 reveal_user(src.reveal_user) {}

const char *
FilterTransformation::GetId(AllocatorPtr alloc) const noexcept
{
	return address.GetId(alloc);
}

void
FilterTransformation::Expand(AllocatorPtr alloc, const MatchData &match_data)
{
	address.Expand(alloc, match_data);
}
