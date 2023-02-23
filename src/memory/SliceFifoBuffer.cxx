// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SliceFifoBuffer.hxx"
#include "SlicePool.hxx"

void
SliceFifoBuffer::Allocate(SlicePool &pool) noexcept
{
	assert(IsNull());

	allocation = pool.Alloc();
	SetBuffer((std::byte *)allocation.data, allocation.size);
}

void
SliceFifoBuffer::Free() noexcept
{
	assert(IsDefined());

	allocation.Free();
	SetNull();
}
