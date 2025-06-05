// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pool.hxx"
#include "LeakDetector.hxx"
#include "util/LeakDetector.hxx"

#include <utility>

#include <stdint.h>

/**
 * A template similar to std::shared_ptr managing an instance
 * allocated from a pool.  Unlike std::shared_ptr, its control block
 * is not thread-safe.
 */
template<typename T>
class SharedPoolPtr final : LeakDetector {
	struct ControlBlock final : PoolLeakDetector {
		struct pool &p;

		uintptr_t ref = 1;

		T value;

		template<typename... Args>
		explicit ControlBlock(struct pool &_p, Args&&... args)
			:PoolLeakDetector(_p),
			 p(_p), value(std::forward<Args>(args)...) {}

		void Ref() noexcept {
			++ref;
		}

		void Unref() noexcept {
			if (--ref == 0)
				DeleteFromPool(p, this);
		}
	};

	ControlBlock *control = nullptr;

	explicit SharedPoolPtr(ControlBlock *_control):control(_control) {}

public:
	SharedPoolPtr() = default;
	SharedPoolPtr(SharedPoolPtr &&src) noexcept
		:control(std::exchange(src.control, nullptr)) {}

	SharedPoolPtr(const SharedPoolPtr &src) noexcept
		:LeakDetector(), control(src.control) {
		if (control != nullptr)
			control->Ref();
	}

	~SharedPoolPtr() noexcept {
		if (control != nullptr)
			control->Unref();
	}

	SharedPoolPtr &operator=(SharedPoolPtr &&src) noexcept {
		using std::swap;
		swap(control, src.control);
		return *this;
	}

	SharedPoolPtr &operator=(const SharedPoolPtr &src) noexcept {
		if (control != nullptr)
			control->Unref();

		control = src.control;
		if (control != nullptr)
			control->Ref();

		return *this;
	}

	void reset() noexcept {
		auto *c = std::exchange(control, nullptr);
		if (c != nullptr)
			c->Unref();
	}

	SharedPoolPtr &operator=(std::nullptr_t) noexcept {
		reset();
		return *this;
	}

	operator bool() const noexcept {
		return control != nullptr;
	}

	constexpr bool operator==(std::nullptr_t n) const noexcept {
		return control == n;
	}

	T &operator*() const noexcept {
		return control->value;
	}

	T *operator->() const noexcept {
		return &control->value;
	}

	template<typename... Args>
	static SharedPoolPtr<T> Make(struct pool &p, Args&&... args) {
		return SharedPoolPtr<T>(NewFromPool<ControlBlock>(p, p, std::forward<Args>(args)...));
	}
};
