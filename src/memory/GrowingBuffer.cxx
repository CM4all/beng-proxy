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

#include "GrowingBuffer.hxx"
#include "pool/pool.hxx"
#include "istream/Bucket.hxx"

#include <algorithm>
#include <cstdarg>

#include <assert.h>
#include <stdio.h>
#include <string.h>

GrowingBuffer::Buffer &
GrowingBuffer::BufferPtr::Allocate() noexcept
{
	assert(buffer == nullptr);

	auto a = allocator.Allocate();
	buffer = ::new(a.data()) Buffer(a.size() - sizeof(*buffer) + sizeof(buffer->data));
	return *buffer;
}

void
GrowingBuffer::BufferPtr::Free() noexcept
{
	assert(buffer != nullptr);

	Check();

	buffer->~Buffer();
	allocator.Free();
	buffer = nullptr;
}

void
GrowingBuffer::BufferPtr::Pop() noexcept
{
	assert(buffer != nullptr);

	Check();
	buffer->next.Check();

	auto next = std::move(buffer->next);
	*this = std::move(next);

	Check();
}

std::span<std::byte>
GrowingBuffer::Buffer::Write() noexcept
{
	Check();

	return {data + fill, size - fill};
}

GrowingBuffer::size_type
GrowingBuffer::Buffer::WriteSome(std::span<const std::byte> src) noexcept
{
	auto dest = Write();
	size_type nbytes = std::min(dest.size(), src.size());
	std::copy_n(src.begin(), nbytes, dest.begin());
	fill += nbytes;
	return nbytes;
}

GrowingBuffer::Buffer &
GrowingBuffer::AppendBuffer() noexcept
{
	tail = tail != nullptr
		? &tail->next.Allocate()
		: &head.Allocate();

	return *tail;
}

void *
GrowingBuffer::BeginWrite(size_type size) noexcept
{
	/* this method is only allowed with "tiny" sizes which fit well
	   into any buffer */
	assert(tail == nullptr || size <= tail->size);

	head.Check();
	if (tail != nullptr)
		tail->Check();

	auto *buffer = tail;
	if (buffer == nullptr || buffer->fill + size > buffer->size)
		buffer = &AppendBuffer();

	assert(buffer->fill + size <= buffer->size);

	return buffer->data + buffer->fill;
}

std::span<std::byte>
GrowingBuffer::BeginWrite() noexcept
{
	head.Check();
	if (tail != nullptr)
		tail->Check();

	auto *buffer = tail;
	if (buffer == nullptr || buffer->IsFull())
		buffer = &AppendBuffer();

	assert(!buffer->IsFull());

	return buffer->Write();
}

void
GrowingBuffer::CommitWrite(size_type size) noexcept
{
	auto *buffer = tail;
	assert(buffer != nullptr);
	assert(buffer->fill + size <= buffer->size);

	buffer->fill += size;
}

void *
GrowingBuffer::Write(size_type length) noexcept
{
	void *ret = BeginWrite(length);
	CommitWrite(length);
	return ret;
}

GrowingBuffer::size_type
GrowingBuffer::WriteSome(const void *p, size_type length) noexcept
{
	auto *buffer = tail;
	if (buffer == nullptr || buffer->IsFull())
		buffer = &AppendBuffer();

	return buffer->WriteSome({(const std::byte *)p, length});
}

void
GrowingBuffer::Write(const void *p, size_type length) noexcept
{
	while (length > 0) {
		size_type nbytes = WriteSome(p, length);
		p = ((const char *)p) + nbytes;
		length -= nbytes;
	}
}

void
GrowingBuffer::Format(const char *fmt, ...) noexcept
{
	va_list ap;
	va_start(ap, fmt);
	const auto reserve_size = (std::size_t)
		vsnprintf(nullptr, 0, fmt, ap) + 1;
	va_end(ap);

	char *p = (char *)BeginWrite(reserve_size);

	va_start(ap, fmt);
	const auto length = (std::size_t)
		vsnprintf(p, reserve_size, fmt, ap);
	va_end(ap);

	assert(length + 1 == reserve_size);

	CommitWrite(length);
}

void
GrowingBuffer::AppendMoveFrom(GrowingBuffer &&src) noexcept
{
	if (src.IsEmpty())
		return;

	tail->next = std::move(src.head);
	tail = src.tail;
	src.tail = nullptr;
}

GrowingBuffer::size_type
GrowingBuffer::GetSize() const noexcept
{
	size_type result = 0;

	ForEachBuffer([&result](std::span<const std::byte> b){
		result += b.size();
	});

	return result;
}

std::span<const std::byte>
GrowingBuffer::Read() const noexcept
{
	if (!head)
		return {};

	head.Check();
	assert(position < head->size);

	return { head->data + position, head->fill - position };
}

void
GrowingBuffer::Consume(size_type length) noexcept
{
	if (length == 0)
		return;

	assert(head);
	head.Check();

	position += length;

	assert(position <= head->fill);

	if (position >= head->fill) {
		head.Pop();
		if (!head)
			tail = nullptr;

		position = 0;
	}
}

void
GrowingBuffer::Skip(size_type length) noexcept
{
	while (length > 0) {
		assert(head);
		head.Check();

		size_type remaining = head->fill - position;
		if (length < remaining) {
			position += length;
			return;
		}

		length -= remaining;
		position = 0;
		head.Pop();
		if (!head)
			tail = nullptr;
	}
}

GrowingBufferReader::GrowingBufferReader(GrowingBuffer &&gb) noexcept
	:buffer(std::move(gb.head))
{
	assert(!buffer || buffer->fill > 0);
}

bool
GrowingBufferReader::IsEOF() const noexcept
{
	assert(!buffer || position <= buffer->fill);

	return !buffer || position == buffer->fill;
}

GrowingBufferReader::size_type
GrowingBufferReader::Available() const noexcept
{
	size_type result = 0;

	ForEachBuffer([&result](std::span<const std::byte> b){
		result += b.size();
	});

	return result;
}

std::span<const std::byte>
GrowingBufferReader::Read() const noexcept
{
	if (!buffer)
		return {};

	assert(position < buffer->fill);

	return { buffer->data + position, buffer->fill - position };
}

void
GrowingBufferReader::Consume(size_type length) noexcept
{
	assert(buffer);

	if (length == 0)
		return;

	position += length;

	assert(position <= buffer->fill);

	if (position >= buffer->fill) {
		buffer.Pop();
		position = 0;
	}
}

void
GrowingBufferReader::Skip(size_type length) noexcept
{
	while (length > 0) {
		assert(buffer);

		size_type remaining = buffer->fill - position;
		if (length < remaining) {
			position += length;
			return;
		}

		length -= remaining;
		buffer.Pop();
		position = 0;
	}
}

void
GrowingBuffer::CopyTo(void *dest) const noexcept
{
	ForEachBuffer([&dest](std::span<const std::byte> b){
		dest = mempcpy(dest, b.data(), b.size());
	});
}

std::span<std::byte>
GrowingBuffer::Dup(struct pool &_pool) const noexcept
{
	size_type length = GetSize();
	if (length == 0)
		return {};

	auto *dest = PoolAlloc<std::byte>(_pool, length);
	CopyTo(dest);

	return { dest, length };
}

void
GrowingBuffer::FillBucketList(IstreamBucketList &list, size_type skip) const noexcept
{
	ForEachBuffer([&list, &skip](std::span<const std::byte> b){
		if (skip >= b.size()) {
			skip -= b.size();
			return;
		}

		if (skip > 0) {
			b = b.subspan(skip);
			skip = 0;
		}

		list.Push(b);
	});
}

GrowingBuffer::size_type
GrowingBuffer::ConsumeBucketList(size_type nbytes) noexcept
{
	size_type result = 0;
	while (nbytes > 0 && head) {
		head.Check();

		size_type available = head->fill - position;
		if (nbytes < available) {
			position += nbytes;
			result += nbytes;
			break;
		}

		result += available;
		nbytes -= available;

		head.Pop();
		if (!head)
			tail = nullptr;

		position = 0;
	}

	return result;
}

void
GrowingBufferReader::FillBucketList(IstreamBucketList &list) const noexcept
{
	ForEachBuffer([&list](std::span<const std::byte> b){
		list.Push(b);
	});
}

GrowingBufferReader::size_type
GrowingBufferReader::ConsumeBucketList(size_type nbytes) noexcept
{
	size_type result = 0;
	while (nbytes > 0 && buffer) {
		size_type available = buffer->fill - position;
		if (nbytes < available) {
			position += nbytes;
			result += nbytes;
			break;
		}

		result += available;
		nbytes -= available;

		buffer.Pop();
		position = 0;
	}

	return result;
}
