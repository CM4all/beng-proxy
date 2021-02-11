/*
 * Copyright 2007-2021 CM4all GmbH
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
#include "util/LeakDetector.hxx"

#include <cstddef>
#include <utility>

/**
 * A template similar to std::unique_ptr managing an instance
 * allocated from a pool.
 */
template<typename T>
class UniquePoolPtr : LeakDetector {
	T *value = nullptr;

public:
	UniquePoolPtr() = default;
	UniquePoolPtr(std::nullptr_t) noexcept {};
	explicit UniquePoolPtr(T *_value) noexcept:value(_value) {}

	UniquePoolPtr(UniquePoolPtr &&src) noexcept
		:value(std::exchange(src.value, nullptr)) {}

	~UniquePoolPtr() noexcept {
		if (value != nullptr)
			value->~T();
	}

	UniquePoolPtr &operator=(UniquePoolPtr &&src) noexcept {
		using std::swap;
		swap(value, src.value);
		return *this;
	}

	UniquePoolPtr &operator=(std::nullptr_t n) noexcept {
		if (value != nullptr)
			value->~T();
		value = n;
		return *this;
	}

	void reset() noexcept {
		auto *v = std::exchange(value, nullptr);
		if (v != nullptr)
			v->~T();
	}

	operator bool() const noexcept {
		return value != nullptr;
	}

	T &operator*() const noexcept {
		return *value;
	}

	T *operator->() const noexcept {
		return value;
	}

	T *get() const noexcept {
		return value;
	}

	template<typename... Args>
	static UniquePoolPtr<T> Make(struct pool &p, Args&&... args) {
		return UniquePoolPtr<T>(NewFromPool<T>(p, std::forward<Args>(args)...));
	}
};
