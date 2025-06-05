// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "DefaultChunkAllocator.hxx"
#include "memory/SlicePool.hxx"
#include "fb_pool.hxx"

#include <cassert>

#ifndef NDEBUG

DefaultChunkAllocator::~DefaultChunkAllocator() noexcept
{
	assert(!allocation.IsDefined());
}

#endif

std::span<std::byte>
DefaultChunkAllocator::Allocate() noexcept
{
	assert(!allocation.IsDefined());

	allocation = fb_pool_get().Alloc();
	return {(std::byte *)allocation.data, allocation.size};
}

void
DefaultChunkAllocator::Free() noexcept
{
	assert(allocation.IsDefined());

	allocation.Free();
}

std::size_t
DefaultChunkAllocator::GetChunkSize() noexcept
{
	return FB_SIZE;
}
