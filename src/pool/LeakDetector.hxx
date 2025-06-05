// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class AllocatorPtr;

#ifndef NDEBUG

#include "util/IntrusiveList.hxx"

/**
 * Derive from this class to verify that its destructor gets called
 * before the #pool gets destroyed.
 */
class PoolLeakDetector
	: public IntrusiveListHook<IntrusiveHookMode::AUTO_UNLINK>
{
	struct pool &ldp;

protected:
	explicit PoolLeakDetector(struct pool &_pool) noexcept;
	explicit PoolLeakDetector(AllocatorPtr alloc) noexcept;

	PoolLeakDetector(const PoolLeakDetector &src):PoolLeakDetector(src.ldp) {}

	/**
	 * This is an arbitrary virtual method only to force RTTI on
	 * the derived class, so we can identify the object type in a
	 * crash dump.
	 */
	virtual void Dummy() noexcept;
};

#else

class PoolLeakDetector {
public:
	explicit PoolLeakDetector(struct pool &) noexcept {}
	explicit PoolLeakDetector(const AllocatorPtr &) noexcept {}
};

#endif
