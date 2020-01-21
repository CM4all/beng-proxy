/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
class SharedPoolPtr : LeakDetector {
	struct ControlBlock : PoolLeakDetector {
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

	operator bool() const noexcept {
		return control != nullptr;
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
