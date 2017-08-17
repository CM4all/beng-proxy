/*
 * Copyright 2007-2017 Content Management AG
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
#include "pool.hxx"
#include "istream/Bucket.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

GrowingBuffer::Buffer &
GrowingBuffer::BufferPtr::Allocate()
{
    assert(buffer == nullptr);

    auto a = allocator.Allocate();
    buffer = ::new(a.data) Buffer(a.size - sizeof(*buffer) + sizeof(buffer->data));
    return *buffer;
}

void
GrowingBuffer::BufferPtr::Free()
{
    assert(buffer != nullptr);

    buffer->~Buffer();
    allocator.Free(buffer);
    buffer = nullptr;
}

void
GrowingBuffer::BufferPtr::Pop()
{
    assert(buffer != nullptr);

    auto next = std::move(buffer->next);
    *this = std::move(next);
}

WritableBuffer<void>
GrowingBuffer::Buffer::Write()
{
    return {data + fill, size - fill};
}

size_t
GrowingBuffer::Buffer::WriteSome(ConstBuffer<void> src)
{
    auto dest = Write();
    size_t nbytes = std::min(dest.size, src.size);
    memcpy(dest.data, src.data, nbytes);
    fill += nbytes;
    return nbytes;
}

GrowingBuffer::Buffer &
GrowingBuffer::AppendBuffer()
{
    tail = tail != nullptr
        ? &tail->next.Allocate()
        : &head.Allocate();

    return *tail;
}


void *
GrowingBuffer::Write(size_t length)
{
    /* this method is only allowed with "tiny" sizes which fit well
       into any buffer */
    assert(tail == nullptr || length <= tail->size);

    auto *buffer = tail;
    if (buffer == nullptr || buffer->fill + length > buffer->size)
        buffer = &AppendBuffer();

    assert(buffer->fill + length <= buffer->size);

    void *ret = buffer->data + buffer->fill;
    buffer->fill += length;

    return ret;
}

size_t
GrowingBuffer::WriteSome(const void *p, size_t length)
{
    auto *buffer = tail;
    if (buffer == nullptr || buffer->IsFull())
        buffer = &AppendBuffer();

    return buffer->WriteSome({p, length});
}

void
GrowingBuffer::Write(const void *p, size_t length)
{
    while (length > 0) {
        size_t nbytes = WriteSome(p, length);
        p = ((const char *)p) + nbytes;
        length -= nbytes;
    }
}

void
GrowingBuffer::Write(const char *p)
{
    Write(p, strlen(p));
}

void
GrowingBuffer::AppendMoveFrom(GrowingBuffer &&src)
{
    if (src.IsEmpty())
        return;

    tail->next = std::move(src.head);
    tail = src.tail;
    src.tail = nullptr;
}

size_t
GrowingBuffer::GetSize() const
{
    size_t result = 0;

    ForEachBuffer([&result](ConstBuffer<void> b){
            result += b.size;
        });

    return result;
}

ConstBuffer<void>
GrowingBuffer::Read() const
{
    if (!head)
        return nullptr;

    assert(position < head->size);

    return { head->data + position, head->fill - position };
}

void
GrowingBuffer::Consume(size_t length)
{
    if (length == 0)
        return;

    assert(head);

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
GrowingBuffer::Skip(size_t length)
{
    while (length > 0) {
        assert(head);

        size_t remaining = head->fill - position;
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

GrowingBufferReader::GrowingBufferReader(GrowingBuffer &&gb)
    :buffer(std::move(gb.head))
{
    assert(!buffer || buffer->fill > 0);
}

bool
GrowingBufferReader::IsEOF() const
{
    assert(!buffer || position <= buffer->fill);

    return !buffer || position == buffer->fill;
}

size_t
GrowingBufferReader::Available() const
{
    size_t result = 0;

    ForEachBuffer([&result](ConstBuffer<void> b){
            result += b.size;
        });

    return result;
}

ConstBuffer<void>
GrowingBufferReader::Read() const
{
    if (!buffer)
        return nullptr;

    assert(position < buffer->fill);

    return { buffer->data + position, buffer->fill - position };
}

void
GrowingBufferReader::Consume(size_t length)
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
GrowingBufferReader::Skip(size_t length)
{
    while (length > 0) {
        assert(buffer);

        size_t remaining = buffer->fill - position;
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
GrowingBuffer::CopyTo(void *dest) const
{
    ForEachBuffer([&dest](ConstBuffer<void> b){
            dest = mempcpy(dest, b.data, b.size);
        });
}

WritableBuffer<void>
GrowingBuffer::Dup(struct pool &_pool) const
{
    size_t length = GetSize();
    if (length == 0)
        return nullptr;

    void *dest = p_malloc(&_pool, length);
    CopyTo(dest);

    return { dest, length };
}

void
GrowingBufferReader::FillBucketList(IstreamBucketList &list) const
{
    ForEachBuffer([&list](ConstBuffer<void> b){
            list.Push(b);
        });
}

size_t
GrowingBufferReader::ConsumeBucketList(size_t nbytes)
{
    size_t result = 0;
    while (nbytes > 0 && buffer) {
        size_t available = buffer->fill - position;
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
