// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
	UniquePoolPtr(std::nullptr_t) noexcept {}
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
