// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "DefaultChunkAllocator.hxx"
#include "util/SpanCast.hxx"

#include <fmt/core.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

class IstreamBucketList;

/**
 * An auto-growing buffer you can write to.
 */
class GrowingBuffer {
	friend class GrowingBufferReader;

	struct Buffer;

	using size_type = std::size_t;

	struct BufferPtr {
		Buffer *buffer = nullptr;
		DefaultChunkAllocator allocator;

		BufferPtr() = default;

		BufferPtr(BufferPtr &&src) noexcept
			:buffer(src.buffer), allocator(std::move(src.allocator)) {
			src.buffer = nullptr;
		}

		~BufferPtr() noexcept {
			if (buffer != nullptr)
				Free();
		}

		BufferPtr &operator=(BufferPtr &&src) noexcept {
			using std::swap;
			swap(buffer, src.buffer);
			swap(allocator, src.allocator);
			return *this;
		}

		operator bool() const noexcept {
			return buffer != nullptr;
		}

		/**
		 * Debug checks.
		 */
		void Check() const noexcept;

		Buffer &Allocate() noexcept;
		void Free() noexcept;

		void Pop() noexcept;

		const Buffer *get() const noexcept {
			return buffer;
		}

		Buffer *get() noexcept {
			return buffer;
		}

		const Buffer &operator*() const noexcept {
			return *buffer;
		}

		Buffer &operator*() noexcept {
			return *buffer;
		}

		const Buffer *operator->() const noexcept {
			return buffer;
		}

		Buffer *operator->() noexcept {
			return buffer;
		}

		void ForEachBuffer(size_type skip,
				   std::invocable<std::span<const std::byte>> auto f) const;
	};

	struct Buffer {
		BufferPtr next;

		const size_type size;
		size_type fill = 0;
		std::byte data[sizeof(size_type)];

		explicit Buffer(size_type _size) noexcept
			:size(_size) {}

		bool IsFull() const noexcept {
			return fill == size;
		}

		/**
		 * Debug checks.
		 */
		void Check() const noexcept {
			assert(size == DefaultChunkAllocator::GetChunkSize() - sizeof(*this) + sizeof(data));
			assert(fill <= size);
		}

		std::span<std::byte> Write() noexcept;
		size_type WriteSome(std::span<const std::byte> src) noexcept;
	};

	BufferPtr head;
	Buffer *tail = nullptr;

	size_type position = 0;

public:
	GrowingBuffer() = default;

	GrowingBuffer(GrowingBuffer &&src) noexcept
		:head(std::move(src.head)), tail(std::exchange(src.tail, nullptr)),
		 position(src.position) {}

	GrowingBuffer &operator=(GrowingBuffer &&src) noexcept {
		using std::swap;
		swap(head, src.head);
		swap(tail, src.tail);
		swap(position, src.position);
		return *this;
	}

	bool IsEmpty() const noexcept {
		return tail == nullptr;
	}

	void Clear() noexcept {
		Release();
	}

	/**
	 * Release the buffer list, which may now be owned by somebody
	 * else.
	 */
	void Release() noexcept {
		if (head)
			head.Free();
		tail = nullptr;
		position = 0;
	}

	/**
	 * Reserve space in the buffer and return a pointer to it.
	 */
	void *BeginWrite(size_type size) noexcept;

	/**
	 * Reserve at least one byte of space and return the writable
	 * range.
	 */
	std::span<std::byte> BeginWrite() noexcept;

	/**
	 * Call this method after the specified number of bytes have
	 * been written to the buffer returned by BeginWrite().
	 */
	void CommitWrite(size_type size) noexcept;

	void *Write(size_type length) noexcept;

	size_type WriteSome(std::span<const std::byte> src) noexcept;
	void Write(std::span<const std::byte> src) noexcept;

	void WriteT(const auto &src) noexcept {
		Write(ReferenceAsBytes(src));
	}

	void Write(std::string_view s) noexcept {
		Write(AsBytes(s));
	}

	void VFmt(fmt::string_view format_str, fmt::format_args args) noexcept;

	template<typename S, typename... Args>
	void Fmt(const S &format_str, Args&&... args) noexcept {
		return VFmt(format_str, fmt::make_format_args(args...));
	}

	void AppendMoveFrom(GrowingBuffer &&src) noexcept;

	/**
	 * Returns the total size of the buffer.
	 */
	[[gnu::pure]]
	size_type GetSize() const noexcept;

	/**
	 * Duplicates the whole buffer (including all chunks) to one
	 * contiguous buffer.
	 */
	std::span<std::byte> Dup(struct pool &pool) const noexcept;

	[[gnu::pure]]
	std::span<const std::byte> Read() const noexcept;

	/**
	 * Skip an arbitrary number of data bytes, which may span over
	 * multiple internal buffers.
	 */
	void Skip(size_type length) noexcept;

	/**
	 * Consume data returned by Read().
	 */
	void Consume(size_type length) noexcept;

	/**
	 * Reserve space at the beginning of an empty buffer, to be
	 * filled by Prepend().
	 */
	void Reserve(size_type length) noexcept {
		assert(IsEmpty());
		assert(!head);
		assert(position == 0);

		BeginWrite(length);
		CommitWrite(length);
		position = length;
	}

	/**
	 * Insert data at the beginning.  This requires a Reserve()
	 * call with at least the specified length.  Returns a pointer
	 * to the new beginning of the buffer where the caller shall
	 * write data.
	 */
	[[nodiscard]]
	void *Prepend(size_type length) noexcept {
		assert(position >= length);
		assert(head);
		assert(head->fill >= position);

		position -= length;
		return head->data + position;
	}

	void FillBucketList(IstreamBucketList &list,
			    size_type skip) const noexcept;
	size_type ConsumeBucketList(size_type nbytes) noexcept;

private:
	Buffer &AppendBuffer() noexcept;

	void CopyTo(void *dest) const noexcept;

	void ForEachBuffer(std::invocable<std::span<const std::byte>> auto f) const {
		head.ForEachBuffer(position, f);
	}
};

inline void
GrowingBuffer::BufferPtr::Check() const noexcept
{
	assert((buffer != nullptr) == allocator.IsDefined());
	if (buffer != nullptr)
		buffer->Check();
}

void
GrowingBuffer::BufferPtr::ForEachBuffer(size_type skip,
					std::invocable<std::span<const std::byte>> auto f) const
{
	for (const auto *i = get(); i != nullptr; i = i->next.get()) {
		i->Check();
		i->next.Check();

		std::span<const std::byte> b{i->data, i->fill};
		if (skip > 0) {
			if (skip >= b.size()) {
				skip -= b.size();
				continue;
			} else {
				b = b.subspan(skip);
				skip = 0;
			}
		}

		f(b);
	}
}

class GrowingBufferReader {
	using size_type = GrowingBuffer::size_type;

	GrowingBuffer::BufferPtr buffer;
	size_type position = 0;

public:
	explicit GrowingBufferReader(GrowingBuffer &&gb) noexcept;

	[[gnu::pure]]
	bool IsEOF() const noexcept;

	[[gnu::pure]]
	size_type Available() const noexcept;

	[[gnu::pure]]
	std::span<const std::byte> Read() const noexcept;

	/**
	 * Consume data returned by Read().
	 */
	void Consume(size_type length) noexcept;

	/**
	 * Skip an arbitrary number of data bytes, which may span over
	 * multiple internal buffers.
	 */
	void Skip(size_type length) noexcept;

	void FillBucketList(IstreamBucketList &list) const noexcept;
	size_type ConsumeBucketList(size_type nbytes) noexcept;

private:
	void ForEachBuffer(std::invocable<std::span<const std::byte>> auto f) const {
		buffer.ForEachBuffer(position, f);
	}
};
