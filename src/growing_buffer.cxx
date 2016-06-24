/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "growing_buffer.hxx"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

GrowingBuffer::Buffer *
GrowingBuffer::Buffer::New(struct pool &pool, size_t size)
{
    Buffer *buffer;
    void *p = p_malloc(&pool, sizeof(*buffer) - sizeof(buffer->data) + size);
    return new(p) Buffer(size);
}

GrowingBuffer::GrowingBuffer(struct pool &_pool, size_t _initial_size)
    :pool(_pool),
     size(_initial_size)
{
}

GrowingBuffer *gcc_malloc
growing_buffer_new(struct pool *pool, size_t initial_size)
{
    return NewFromPool<GrowingBuffer>(*pool, *pool, initial_size);
}

void
GrowingBuffer::AppendBuffer(Buffer &buffer)
{
    assert(buffer.next == nullptr);

    if (tail != nullptr) {
        tail->next = &buffer;
        tail = &buffer;
    } else {
        head = tail = &buffer;
    }
}

void *
GrowingBuffer::Write(size_t length)
{
    void *ret;

    assert(size > 0);

    auto *buffer = tail;
    if (buffer == nullptr || buffer->fill + length > buffer->size) {
        size_t new_size = std::max(length, size);
        buffer = Buffer::New(pool, new_size);
        AppendBuffer(*buffer);
    }

    assert(buffer->fill + length <= buffer->size);

    ret = buffer->data + buffer->fill;
    buffer->fill += length;

    return ret;
}

void
GrowingBuffer::Write(const void *p, size_t length)
{
    memcpy(Write(length), p, length);
}

void
GrowingBuffer::Write(const char *p)
{
    Write(p, strlen(p));
}

void
GrowingBuffer::AppendMoveFrom(GrowingBuffer &&src)
{
    tail->next = src.head;
    tail = src.tail;
}

size_t
GrowingBuffer::GetSize() const
{
    size_t result = 0;

    for (const auto *buffer = head;
         buffer != nullptr; buffer = buffer->next)
        result += buffer->fill;

    return result;
}

GrowingBufferReader::GrowingBufferReader(const GrowingBuffer &gb)
#ifndef NDEBUG
    :growing_buffer(&gb)
#endif
{
    assert(gb.head == nullptr || gb.head->fill > 0);

    buffer = gb.head;

    assert(buffer == nullptr || buffer->fill > 0);

    position = 0;
}

void
GrowingBufferReader::Update(const GrowingBuffer &gb)
{
    assert(position == 0 || position <= buffer->fill);

    if (buffer == nullptr)
        buffer = gb.head;
    else if (position == buffer->fill &&
             buffer->next != nullptr) {
        /* the reader was at the end of all buffers, but then a new
           buffer was appended */
        buffer = buffer->next;
        position = 0;
    }
}

bool
GrowingBufferReader::IsEOF() const
{
    assert(buffer == nullptr || position <= buffer->fill);

    return buffer == nullptr || position == buffer->fill;
}

size_t
GrowingBufferReader::Available() const
{
    if (buffer == nullptr)
        return 0;

    assert(position <= buffer->fill);

    size_t available = buffer->fill - position;
    for (const auto *b = buffer->next; b != nullptr; b = b->next) {
        assert(b->fill > 0);

        available += b->fill;
    }

    return available;
}

ConstBuffer<void>
GrowingBufferReader::Read() const
{
    if (buffer == nullptr)
        return nullptr;

    assert(buffer != nullptr);

    const auto *b = buffer;

    if (position >= b->fill) {
        assert(position == b->fill);
        assert(buffer->next == nullptr);
        return nullptr;
    }

    return { b->data + position, b->fill - position };
}

void
GrowingBufferReader::Consume(size_t length)
{
    assert(buffer != nullptr);

    if (length == 0)
        return;

    position += length;

    assert(position <= buffer->fill);

    if (position >= buffer->fill) {
        if (buffer->next == nullptr)
            return;

        buffer = buffer->next;
        position = 0;
    }
}

ConstBuffer<void>
GrowingBufferReader::PeekNext() const
{
    const auto *b = buffer;
    if (b == nullptr)
        return nullptr;

    assert(b->fill > 0);

    b = b->next;

    if (b == nullptr)
        return nullptr;

    assert(b->fill > 0);
    return { b->data, b->fill };
}

void
GrowingBufferReader::Skip(size_t length)
{
    while (length > 0) {
        assert(buffer != nullptr);

        size_t remaining = buffer->fill - position;
        if (length < remaining ||
            (length == remaining && buffer->next == nullptr)) {
            position += length;
            return;
        }

        length -= remaining;

        if (buffer->next == nullptr) {
            assert(position + remaining == length);
            position = length;
            return;
        }

        assert(buffer->next != nullptr);
        buffer = buffer->next;
        position = 0;
    }
}

void
GrowingBuffer::CopyTo(void *dest) const
{
    for (const auto *buffer = head; buffer != nullptr;
         buffer = buffer->next)
        dest = mempcpy(dest, buffer->data, buffer->fill);
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
