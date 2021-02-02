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

#include "pool/pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringView.hxx"

#include <algorithm>
#include <type_traits>

#include <string.h>

class SocketAddress;

class AllocatorPtr {
	struct pool &pool;

public:
	constexpr AllocatorPtr(struct pool &_pool) noexcept :pool(_pool) {}

	char *Dup(const char *src) const noexcept {
		return p_strdup(&pool, src);
	}

	const char *CheckDup(const char *src) const noexcept {
		return p_strdup_checked(&pool, src);
	}

	template<typename... Args>
	char *Concat(Args&&... args) const noexcept {
		const size_t length = ConcatLength(args...);
		char *result = NewArray<char>(length + 1);
		*ConcatCopy(result, args...) = 0;
		return result;
	}

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

	ConstBuffer<void> Dup(ConstBuffer<void> src) const noexcept;

	template<typename T>
	ConstBuffer<T> Dup(ConstBuffer<T> src) const noexcept {
		static_assert(std::is_trivial_v<T>);

		return ConstBuffer<T>::FromVoid(Dup(src.ToVoid()));
	}

	/**
	 * Clone an array, invoking a constructor accepting "const
	 * AllocatorPtr &, const T &" for each item.
	 */
	template<typename T>
	ConstBuffer<T> CloneArray(ConstBuffer<T> src) const noexcept {
		static_assert(std::is_trivially_destructible_v<T>);

		if (src == nullptr)
			return nullptr;

		auto *dest = NewArray<T>(src.size);
		std::transform(src.begin(), src.end(), dest, [this](const T &i){
			return T{*this, i};
		});

		return {dest, src.size};
	}

	StringView Dup(StringView src) const noexcept;
	const char *DupZ(StringView src) const noexcept;

	const char *DupToLower(StringView src) const noexcept {
		return p_strdup_lower(pool, src);
	}

	SocketAddress Dup(SocketAddress src) const noexcept;

private:
	template<typename... Args>
	static size_t ConcatLength(const char *s, Args... args) noexcept {
		return strlen(s) + ConcatLength(args...);
	}

	template<typename... Args>
	static size_t ConcatLength(char, Args... args) noexcept {
		return 1 + ConcatLength(args...);
	}

	template<typename... Args>
	static constexpr size_t ConcatLength(StringView s, Args... args) noexcept {
		return s.size + ConcatLength(args...);
	}

	template<typename... Args>
	static constexpr size_t ConcatLength(ConstBuffer<StringView> s,
					     Args... args) noexcept {
		size_t length = ConcatLength(args...);
		for (const auto &i : s)
			length += i.size;
		return length;
	}

	static constexpr size_t ConcatLength() noexcept {
		return 0;
	}

	template<typename... Args>
	static char *ConcatCopy(char *p, const char *s, Args... args) noexcept {
		return ConcatCopy(stpcpy(p, s), args...);
	}

	template<typename... Args>
	static char *ConcatCopy(char *p, char ch, Args... args) noexcept {
		*p++ = ch;
		return ConcatCopy(p, args...);
	}

	template<typename... Args>
	static char *ConcatCopy(char *p, StringView s, Args... args) noexcept {
		return ConcatCopy((char *)mempcpy(p, s.data, s.size), args...);
	}

	template<typename... Args>
	static char *ConcatCopy(char *p, ConstBuffer<StringView> s, Args... args) noexcept {
		for (const auto &i : s)
			p = ConcatCopy(p, i);
		return ConcatCopy(p, args...);
	}

	template<typename... Args>
	static char *ConcatCopy(char *p) noexcept {
		return p;
	}
};
