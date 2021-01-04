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

#ifndef SHM_STRING_HXX
#define SHM_STRING_HXX

#include "util/StringView.hxx"

#include <new>
#include <utility>

struct dpool;

/**
 * A string allocated from shared memory.
 *
 * An instance is always in a well-defined state; it cannot be uninitialized.
 */
class DString {
	char *value = nullptr;

	explicit constexpr DString(char *_value):value(_value) {}

public:
	/**
	 * Construct a "nulled" instance.
	 */
	DString() = default;

	constexpr DString(std::nullptr_t) {}

	/**
	 * Throws std::bad_alloc on error.
	 */
	DString(struct dpool &pool, StringView src) {
		Set(pool, src);
	}

	/**
	 * Throws std::bad_alloc on error.
	 */
	DString(struct dpool &pool, const DString &src)
		:DString(pool, src.value) {}

	static DString Donate(char *_value) {
		return DString(_value);
	}

	DString(DString &&src):value(src.value) {
		src.value = nullptr;
	}

	DString &operator=(DString &&src) noexcept {
		std::swap(value, src.value);
		return *this;
	}

	constexpr operator bool() const noexcept {
		return value != nullptr;
	}

	constexpr operator const char *() const noexcept {
		return value;
	}

	constexpr const char *c_str() const noexcept {
		return value;
	}

	operator StringView() const noexcept {
		return value;
	}

	/* note: this method is only necessary to work around a GCC 8 bug
	   ("error: call of overloaded 'StringView(const DString&)' is
	   ambiguous") */
	auto ToStringView() const noexcept {
		return value;
	}

	void Clear(struct dpool &pool) noexcept;

	/**
	 * Assign a new value.  Throws std::bad_alloc if memory allocation
	 * fails.
	 */
	void Set(struct dpool &pool, StringView _value);

	/**
	 * Assign a new value.  Returns false if memory allocation fails.
	 */
	bool SetNoExcept(struct dpool &pool, StringView _value) noexcept;
};

#endif
