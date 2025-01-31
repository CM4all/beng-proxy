// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GrowingBuffer.hxx"
#include "pool/pool.hxx"
#include "istream/Bucket.hxx"

#include <algorithm>
#include <cstdarg>

#include <assert.h>
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
GrowingBuffer::WriteSome(std::span<const std::byte> src) noexcept
{
	auto *buffer = tail;
	if (buffer == nullptr || buffer->IsFull())
		buffer = &AppendBuffer();

	return buffer->WriteSome(src);
}

void
GrowingBuffer::Write(std::span<const std::byte> src) noexcept
{
	while (!src.empty()) {
		size_type nbytes = WriteSome(src);
		src = src.subspan(nbytes);
	}
}

void
GrowingBuffer::VFmt(fmt::string_view format_str, fmt::format_args args) noexcept
{
	auto w = BeginWrite();

	const std::size_t size =
		fmt::vformat_to_n((char *)w.data(), w.size(),
				  format_str, args).size;

	if (size > w.size())
		fmt::vformat_to((char *)BeginWrite(size), format_str, args);

	CommitWrite(size);
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
	:buffer(std::move(gb.head)), position(gb.position)
{
	assert(!buffer || buffer->fill > 0);
	assert(!buffer || position <= buffer->fill);

	if (buffer && position >= buffer->fill)
		/* the first buffer has already been consumed; this
                   can happen if Reserve() was called but no other
                   data has ever been added */
		buffer.Pop();
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
