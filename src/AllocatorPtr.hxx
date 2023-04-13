// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pool/pool.hxx"

#include <algorithm>
#include <span>
#include <string_view>
#include <type_traits>

#include <string.h>

class SocketAddress;

class AllocatorPtr {
	struct pool &pool;

public:
	constexpr AllocatorPtr(struct pool &_pool) noexcept :pool(_pool) {}

	struct pool &GetPool() const noexcept {
		return pool;
	}

	char *Dup(const char *src) const noexcept {
		return p_strdup(&pool, src);
	}

	const char *CheckDup(const char *src) const noexcept {
		return src == nullptr ? nullptr : Dup(src);
	}

	template<typename... Args>
	char *Concat(Args... args) const noexcept {
		const size_t length = (ConcatLength(args) + ...);
		char *result = NewArray<char>(length + 1);
		*ConcatCopyAll(result, args...) = 0;
		return result;
	}

	/**
	 * Concatenate all parameters into a newly allocated
	 * std::string_view.
	 */
	template<typename... Args>
	std::string_view ConcatView(Args... args) noexcept {
		const size_t length = (ConcatLength(args) + ...);
		char *result = NewArray<char>(length);
		ConcatCopyAll(result, args...);
		return {result, length};
	}

	/**
	 * Allocate a new buffer with data concatenated from the given source
	 * buffers.  If one is empty, this may return a pointer to the other
	 * buffer.
	 */
	std::span<const std::byte> LazyConcat(std::span<const std::byte> a,
					      std::span<const std::byte> b) const noexcept;

	template<typename T, typename... Args>
	T *New(Args&&... args) const noexcept {
		return NewFromPool<T>(pool, std::forward<Args>(args)...);
	}

	template<typename T>
	T *NewArray(size_t n) const noexcept {
		static_assert(std::is_trivial_v<T>);

		return PoolAlloc<T>(pool, n);
	}

	void *Dup(const void *data, size_t size) const noexcept {
		return p_memdup(&pool, data, size);
	}

	std::span<const std::byte> Dup(std::span<const std::byte> src) const noexcept;

	template<typename T>
	std::span<const T> Dup(std::span<const T> src) const noexcept {
		auto dest = Dup(std::as_bytes(src));
		return {
			(const T *)(const void *)(dest.data()),
			dest.size() / sizeof(T),
		};
	}

	/**
	 * Copy all items of a std::initializer_list to a newly
	 * allocated array wrapped in a std::span.
	 */
	template<typename T>
	std::span<const T> Dup(std::initializer_list<T> src) const noexcept {
		static_assert(std::is_trivially_destructible_v<T>);

		auto *dest = NewArray<T>(src.size());
		std::copy(src.begin(), src.end(), dest);

		return {dest, src.size()};
	}

	/**
	 * Construct an array with items from a std::initializer_list.
	 */
	template<typename T, typename U>
	std::span<const T> ConstructArray(std::initializer_list<U> src) const noexcept {
		static_assert(std::is_trivially_destructible_v<T>);

		auto *dest = NewArray<T>(src.size());
		std::transform(src.begin(), src.end(), dest, [](const U &i){
			return T{i};
		});

		return {dest, src.size()};
	}

	/**
	 * Clone an array, invoking a constructor accepting "const
	 * AllocatorPtr &, const T &" for each item.
	 */
	template<typename T>
	std::span<const T> CloneArray(std::span<const T> src) const noexcept {
		static_assert(std::is_trivially_destructible_v<T>);

		if (src.data() == nullptr)
			return {};

		auto *dest = NewArray<T>(src.size());
		std::transform(src.begin(), src.end(), dest, [this](const T &i){
			return T{*this, i};
		});

		return {dest, src.size()};
	}

	std::string_view Dup(std::string_view src) const noexcept;
	const char *DupZ(std::string_view src) const noexcept;

	const char *DupToLower(std::string_view src) const noexcept {
		return p_strdup_lower(pool, src);
	}

	SocketAddress Dup(SocketAddress src) const noexcept;

private:
	[[gnu::pure]]
	static size_t ConcatLength(const char *s) noexcept {
		return strlen(s);
	}

	static constexpr size_t ConcatLength(char) noexcept {
		return 1;
	}

	static constexpr size_t ConcatLength(std::string_view sv) noexcept {
		return sv.size();
	}

	static constexpr size_t ConcatLength(std::span<const std::string_view> s) noexcept {
		size_t length = 0;
		for (const auto &i : s)
			length += i.size();
		return length;
	}

	static char *ConcatCopy(char *p, const char *s) noexcept {
		return stpcpy(p, s);
	}

	static char *ConcatCopy(char *p, char ch) noexcept {
		*p++ = ch;
		return p;
	}

	static char *ConcatCopy(char *p, std::string_view sv) noexcept {
		return std::copy(sv.begin(), sv.end(), p);
	}

	static char *ConcatCopy(char *p, std::span<const std::string_view> s) noexcept {
		for (const auto &i : s)
			p = ConcatCopy(p, i);
		return p;
	}

	template<typename... Args>
	static char *ConcatCopyAll(char *p, Args... args) noexcept {
		((p = ConcatCopy(p, args)), ...);
		return p;
	}
};
