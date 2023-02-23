// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "LeakDetector.hxx"
#include "pool.hxx"
#include "AllocatorPtr.hxx"

#ifndef NDEBUG

PoolLeakDetector::PoolLeakDetector(struct pool &_pool) noexcept
	:ldp(_pool)
{
	pool_register_leak_detector(ldp, *this);
}

PoolLeakDetector::PoolLeakDetector(AllocatorPtr alloc) noexcept
	:PoolLeakDetector(alloc.GetPool()) {}

void
PoolLeakDetector::Dummy() noexcept
{
}

#endif
