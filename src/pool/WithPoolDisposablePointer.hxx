// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Holder.hxx"
#include "util/Cast.hxx"
#include "util/DisposablePointer.hxx"
#include "util/LeakDetector.hxx"

/**
 * Helper class which creates a #DisposablePointer pointing to an
 * instance of the specified type allocated with a #pool; disposing of
 * the pointer will not only call its destructor, but also
 * unreferences the pool.
 */
template<typename T>
class WithPoolDisposablePointer final : PoolHolder, LeakDetector {
	[[no_unique_address]]
	T value;

public:
	template<typename P, typename... Args>
	WithPoolDisposablePointer(P &&_pool, Args&&... args) noexcept
		:PoolHolder(std::forward<P>(_pool)),
		 value(GetPool(), std::forward<Args>(args)...) {}

	template<typename P, typename... Args>
	static TypedDisposablePointer<T> New(P &&_pool, Args&&... args) {
		auto *w = NewFromPool<WithPoolDisposablePointer>(std::forward<P>(_pool),
								 std::forward<Args>(args)...);
		return {&w->value, Dispose};
	}

private:
	static void Dispose(void *p) noexcept {
		auto &t = *reinterpret_cast<T *>(p);
		auto &w = ContainerCast(t, &WithPoolDisposablePointer::value);
		w.~WithPoolDisposablePointer();
	}
};
