// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "DefaultFifoBuffer.hxx"
#include "memory/fb_pool.hxx"

void
DefaultFifoBuffer::Allocate() noexcept
{
	SliceFifoBuffer::Allocate(fb_pool_get());
}

void
DefaultFifoBuffer::AllocateIfNull() noexcept
{
	SliceFifoBuffer::AllocateIfNull(fb_pool_get());
}

void
DefaultFifoBuffer::CycleIfEmpty() noexcept
{
	SliceFifoBuffer::CycleIfEmpty(fb_pool_get());
}
