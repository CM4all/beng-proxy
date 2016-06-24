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
     size(_initial_size),
     head(Buffer::New(pool, size)),
     tail(head)
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

    tail->next = &buffer;
    tail = &buffer;
}

void *
GrowingBuffer::Write(size_t length)
{
    void *ret;

    assert(size > 0);

    auto *buffer = tail;
    if (buffer->length + length > size) {
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
    assert(gb.head == nullptr ||
           gb.head->length > 0 || gb.head->next == nullptr ||
           (gb.head->next != nullptr &&
            gb.size > gb.initial_size &&
            gb.head->next->length > gb.initial_size));

    buffer = gb.head;
    if (buffer->length == 0 && buffer->next != nullptr)
        buffer = buffer->next;

    position = 0;
}

void
GrowingBufferReader::Update()
{
    assert(buffer != nullptr);
    assert(position <= buffer->length);

    if (position == buffer->length &&
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
    assert(buffer != nullptr);
    assert(position <= buffer->length);

    return position == buffer->length;
}

size_t
GrowingBufferReader::Available() const
{
    assert(buffer != nullptr);
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
    assert(buffer != nullptr);

    const auto *b = buffer;

    if (b->length == 0 && b->next != nullptr) {
        /* skip the empty first buffer that was too small */
        assert(b == growing_buffer->head);
        assert(position == 0);

        b = b->next;
    }

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

    if (buffer->length == 0 && buffer->next != nullptr) {
        /* skip the empty first buffer that was too small */
        assert(buffer == growing_buffer->head);
        assert(position == 0);

        buffer = buffer->next;
    }

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
    assert(buffer != nullptr);

    const auto *b = buffer;

    if (b->length == 0 && b->next != nullptr) {
        /* skip the empty first buffer that was too small */
        assert(b == growing_buffer->head);
        assert(position == 0);

        b = b->next;
    }

    b = b->next;

    if (b == nullptr)
        return nullptr;

    return { b->data, b->length };
}

void
GrowingBufferReader::Skip(size_t length)
{
    assert(buffer != nullptr);

    while (length > 0) {
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
