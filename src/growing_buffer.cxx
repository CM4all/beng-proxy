/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "growing_buffer.hxx"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <assert.h>
#include <string.h>

GrowingBuffer::Buffer *
GrowingBuffer::Buffer::New(struct pool &pool, size_t size)
{
    Buffer *buffer;
    void *p = p_malloc(&pool, sizeof(*buffer) - sizeof(buffer->data) + size);
    return new(p) Buffer();
}

GrowingBuffer::GrowingBuffer(struct pool &_pool, size_t _initial_size)
    :pool(_pool),
#ifndef NDEBUG
     initial_size(_initial_size),
#endif
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
    if (buffer == nullptr || buffer->length + length > size) {
        if (size < length)
            size = length; /* XXX round up? */
        buffer = Buffer::New(pool, size);
        AppendBuffer(*buffer);
    }

    assert(buffer->length + length <= size);

    ret = buffer->data + buffer->length;
    buffer->length += length;

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
    size = src.size;
}

size_t
GrowingBuffer::GetSize() const
{
    size_t result = 0;

    for (const auto *buffer = head;
         buffer != nullptr; buffer = buffer->next)
        result += buffer->length;

    return result;
}

GrowingBufferReader::GrowingBufferReader(const GrowingBuffer &gb)
#ifndef NDEBUG
    :growing_buffer(&gb)
#endif
{
    assert(gb.head == nullptr || gb.head->length > 0);

    buffer = gb.head;

    assert(buffer == nullptr || buffer->length > 0);

    position = 0;
}

void
GrowingBufferReader::Update(const GrowingBuffer &gb)
{
    assert(position == 0 || position <= buffer->length);

    if (buffer == nullptr)
        buffer = gb.head;
    else if (position == buffer->length &&
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
    assert(buffer == nullptr || position <= buffer->length);

    return buffer == nullptr || position == buffer->length;
}

size_t
GrowingBufferReader::Available() const
{
    if (buffer == nullptr)
        return 0;

    assert(position <= buffer->length);

    size_t available = buffer->length - position;
    for (const auto *b = buffer->next; b != nullptr; b = b->next) {
        assert(b->length > 0);

        available += b->length;
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

    if (position >= b->length) {
        assert(position == b->length);
        assert(buffer->next == nullptr);
        return nullptr;
    }

    return { b->data + position, b->length - position };
}

void
GrowingBufferReader::Consume(size_t length)
{
    assert(buffer != nullptr);

    if (length == 0)
        return;

    position += length;

    assert(position <= buffer->length);

    if (position >= buffer->length) {
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

    assert(b->length > 0);

    b = b->next;

    if (b == nullptr)
        return nullptr;

    assert(b->length > 0);
    return { b->data, b->length };
}

void
GrowingBufferReader::Skip(size_t length)
{
    while (length > 0) {
        assert(buffer != nullptr);

        size_t remaining = buffer->length - position;
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
        dest = mempcpy(dest, buffer->data, buffer->length);
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
