/*
 * Copyright 2007-2022 CM4all GmbH
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

#include <cstddef>
#include <span>

struct pool;
template<typename T> struct ConstBuffer;
struct StringView;

/**
 * A buffer which grows automatically.  Compared to growing_buffer, it
 * is optimized to be read as one complete buffer, instead of many
 * smaller chunks.  Additionally, it can be reused.
 */
class ExpansibleBuffer {
	struct pool &pool;
	char *buffer;
	const size_t hard_limit;
	size_t max_size;
	size_t size = 0;

public:
	/**
	 * @param _hard_limit the buffer will refuse to grow beyond this size
	 */
	ExpansibleBuffer(struct pool &_pool,
			 size_t initial_size, size_t _hard_limit) noexcept;

	ExpansibleBuffer(const ExpansibleBuffer &) = delete;
	ExpansibleBuffer &operator=(const ExpansibleBuffer &) = delete;

	bool IsEmpty() const noexcept {
		return size == 0;
	}

	size_t GetSize() const noexcept {
		return size;
	}

	void Clear() noexcept;

	/**
	 * @return nullptr if the operation would exceed the hard limit
	 */
	void *Write(size_t length) noexcept;

	/**
	 * @return false if the operation would exceed the hard limit
	 */
	bool Write(const void *p, size_t length) noexcept;

	/**
	 * @return false if the operation would exceed the hard limit
	 */
	bool Write(const char *p) noexcept;

	/**
	 * @return false if the operation would exceed the hard limit
	 */
	bool Set(const void *p, size_t new_size) noexcept;

	bool Set(StringView p) noexcept;

	[[gnu::pure]]
	ConstBuffer<void> Read() const noexcept;

	[[gnu::pure]]
	const char *ReadString() noexcept;

	[[gnu::pure]]
	StringView ReadStringView() const noexcept;

	std::span<std::byte> Dup(struct pool &_pool) const noexcept;

	char *StringDup(struct pool &_pool) const noexcept;

private:
	bool Resize(size_t new_max_size) noexcept;
};
