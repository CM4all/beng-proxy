/*
 * Copyright (C) 2015-2017 Max Kellermann <max@duempel.org>
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

#ifndef STRING_BUILDER_HXX
#define STRING_BUILDER_HXX

#include <utility>

#include <stddef.h>

/**
 * Fills a string buffer incrementally by appending more data to the
 * end, truncating the string if the buffer is full.
 */
template<typename T=char>
class StringBuilder {
	typedef T value_type;
	typedef T *pointer;
	typedef const T *const_pointer;
	typedef size_t size_type;

	pointer p;
	const pointer end;

	static constexpr value_type SENTINEL = '\0';

public:
	constexpr StringBuilder(pointer _p, pointer _end):p(_p), end(_end) {}
	constexpr StringBuilder(pointer _p, size_type size)
		:p(_p), end(p + size) {}

	constexpr size_type GetRemainingSize() const {
		return end - p;
	}

	constexpr bool IsFull() const {
		return p >= end - 1;
	}

	/**
	 * This class gets thrown when the buffer would overflow by an
	 * operation.  The buffer is then in an undefined state.
	 */
	class Overflow {};

	constexpr bool CanAppend(size_type length) const {
		return p + length < end;
	}

	void CheckAppend(size_type length) const {
		if (!CanAppend(length))
			throw Overflow();
	}

	void Append(T ch) {
		CheckAppend(1);

		*p++ = ch;
		*p = SENTINEL;
	}

	void Append(const_pointer src);
	void Append(const_pointer src, size_t length);
};

#endif
