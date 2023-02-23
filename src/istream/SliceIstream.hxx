// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "MemoryIstream.hxx"
#include "memory/SliceAllocation.hxx"

class SliceBuffer;

/**
 * A variant of #MemoryIstream which frees memory to a #SlicePool.
 */
class SliceIstream final : public MemoryIstream {
	SliceAllocation allocation;

public:
	SliceIstream(struct pool &p, SliceAllocation &&_allocation,
		     size_t _size) noexcept
		:MemoryIstream(p,
			       {(const std::byte *)_allocation.data, _size}),
		 allocation(std::move(_allocation)) {}

	SliceIstream(struct pool &p, SliceBuffer &&src) noexcept;
};
