// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstddef>
#include <span>
#include <string_view>

struct pool;

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
	bool Write(std::string_view src) noexcept;

	/**
	 * @return false if the operation would exceed the hard limit
	 */
	bool Set(const void *p, size_t new_size) noexcept;

	bool Set(std::string_view p) noexcept;

	[[gnu::pure]]
	std::span<const std::byte> Read() const noexcept;

	[[gnu::pure]]
	const char *ReadString() noexcept;

	[[gnu::pure]]
	std::string_view ReadStringView() const noexcept;

	std::span<std::byte> Dup(struct pool &_pool) const noexcept;

	char *StringDup(struct pool &_pool) const noexcept;

private:
	bool Resize(size_t new_max_size) noexcept;
};
