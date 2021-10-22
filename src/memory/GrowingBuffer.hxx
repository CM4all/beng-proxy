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

#include "DefaultChunkAllocator.hxx"
#include "util/ConstBuffer.hxx"

#include <cstddef>
#include <utility>

#include <stdint.h>

template<typename T> struct ConstBuffer;
template<typename T> struct WritableBuffer;
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

		template<typename F>
		void ForEachBuffer(size_type skip, F &&f) const;
	};

	struct Buffer {
		BufferPtr next;

		const size_type size;
		size_type fill = 0;
		uint8_t data[sizeof(size_type)];

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

		WritableBuffer<void> Write() noexcept;
		size_type WriteSome(ConstBuffer<void> src) noexcept;
	};

	BufferPtr head;
	Buffer *tail = nullptr;

	size_type position = 0;

public:
	GrowingBuffer() = default;

	GrowingBuffer(GrowingBuffer &&src) noexcept
		:head(std::move(src.head)), tail(src.tail) {
		src.tail = nullptr;
	}

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
	 * Call this method after the specified number of bytes have
	 * been written to the buffer returned by BeginWrite().
	 */
	void CommitWrite(size_type size) noexcept;

	void *Write(size_type length) noexcept;

	size_type WriteSome(const void *p, size_type length) noexcept;
	void Write(const void *p, size_type length) noexcept;

	void Write(const char *p) noexcept;

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
	WritableBuffer<void> Dup(struct pool &pool) const noexcept;

	[[gnu::pure]]
	ConstBuffer<void> Read() const noexcept;

	/**
	 * Skip an arbitrary number of data bytes, which may span over
	 * multiple internal buffers.
	 */
	void Skip(size_type length) noexcept;

	/**
	 * Consume data returned by Read().
	 */
	void Consume(size_type length) noexcept;

	void FillBucketList(IstreamBucketList &list,
			    size_type skip) const noexcept;
	size_type ConsumeBucketList(size_type nbytes) noexcept;

private:
	Buffer &AppendBuffer() noexcept;

	void CopyTo(void *dest) const noexcept;

	template<typename F>
	void ForEachBuffer(F &&f) const {
		head.ForEachBuffer(position, std::forward<F>(f));
	}
};

inline void
GrowingBuffer::BufferPtr::Check() const noexcept
{
	assert((buffer != nullptr) == allocator.IsDefined());
	if (buffer != nullptr)
		buffer->Check();
}

template<typename F>
void
GrowingBuffer::BufferPtr::ForEachBuffer(size_type skip, F &&f) const
{
	for (const auto *i = get(); i != nullptr; i = i->next.get()) {
		i->Check();
		i->next.Check();

		ConstBuffer<uint8_t> b(i->data, i->fill);
		if (skip > 0) {
			if (skip >= b.size) {
				skip -= b.size;
				continue;
			} else {
				b.skip_front(skip);
				skip = 0;
			}
		}

		f(b.ToVoid());
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
	ConstBuffer<void> Read() const noexcept;

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
	template<typename F>
	void ForEachBuffer(F &&f) const {
		buffer.ForEachBuffer(position, std::forward<F>(f));
	}
};
