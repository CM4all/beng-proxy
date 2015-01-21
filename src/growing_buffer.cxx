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

struct buffer {
    struct buffer *next;
    size_t length;
    char data[sizeof(size_t)];
};

struct GrowingBuffer {
    struct pool *pool;

#ifndef NDEBUG
    size_t initial_size;
#endif

    size_t size;
    struct buffer *current, *tail, first;
};

GrowingBuffer *gcc_malloc
growing_buffer_new(struct pool *pool, size_t initial_size)
{
    GrowingBuffer *gb = (GrowingBuffer *)
        p_malloc(pool, sizeof(*gb) - sizeof(gb->first.data) + initial_size);

    gb->pool = pool;

#ifndef NDEBUG
    gb->initial_size = initial_size;
#endif

    gb->size = initial_size;
    gb->current = &gb->first;
    gb->tail = &gb->first;
    gb->first.next = nullptr;
    gb->first.length = 0;

    return gb;
}

static void
growing_buffer_append_buffer(GrowingBuffer *gb, struct buffer *buffer)
{
    assert(gb != nullptr);
    assert(buffer != nullptr);
    assert(buffer->next == nullptr);

    gb->tail->next = buffer;
    gb->tail = buffer;
}

void *
growing_buffer_write(GrowingBuffer *gb, size_t length)
{
    struct buffer *buffer = gb->tail;
    void *ret;

    assert(gb->size > 0);

    if (buffer->length + length > gb->size) {
        if (gb->size < length)
            gb->size = length; /* XXX round up? */
        buffer = (struct buffer *)
            p_malloc(gb->pool,
                     sizeof(*buffer) - sizeof(buffer->data) + gb->size);
        buffer->next = nullptr;
        buffer->length = 0;

        growing_buffer_append_buffer(gb, buffer);
    }

    assert(buffer->length + length <= gb->size);

    ret = buffer->data + buffer->length;
    buffer->length += length;

    return ret;
}

void
growing_buffer_write_buffer(GrowingBuffer *gb, const void *p, size_t length)
{
    memcpy(growing_buffer_write(gb, length), p, length);
}

void
growing_buffer_write_string(GrowingBuffer *gb, const char *p)
{
    growing_buffer_write_buffer(gb, p, strlen(p));
}

void
growing_buffer_cat(GrowingBuffer *dest, GrowingBuffer *src)
{
    dest->tail->next = &src->first;
    dest->tail = src->tail;
    dest->size = src->size;
}

size_t
growing_buffer_size(const GrowingBuffer *gb)
{
    size_t size = 0;

    for (const struct buffer *buffer = &gb->first;
         buffer != nullptr; buffer = buffer->next)
        size += buffer->length;

    return size;
}

GrowingBufferReader::GrowingBufferReader(const GrowingBuffer &gb)
#ifndef NDEBUG
    :growing_buffer(&gb)
#endif
{
    assert(gb.first.length > 0 || gb.first.next == nullptr ||
           (gb.first.next != nullptr &&
            gb.size > gb.initial_size &&
            gb.first.next->length > gb.initial_size));

    buffer = &gb.first;
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
    for (const struct buffer *b = buffer->next; b != nullptr; b = b->next) {
        assert(b->length > 0);

        available += b->length;
    }

    return available;
}

ConstBuffer<void>
GrowingBufferReader::Read() const
{
    assert(buffer != nullptr);

    const struct buffer *b = buffer;

    if (b->length == 0 && b->next != nullptr) {
        /* skip the empty first buffer that was too small */
        assert(b == &growing_buffer->first);
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
        assert(buffer == &growing_buffer->first);
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

        assert(buffer->next != nullptr);
        buffer = buffer->next;
        position = 0;
    }
}

static void *
growing_buffer_copy(void *dest0, const GrowingBuffer *gb)
{
    unsigned char *dest = (unsigned char *)dest0;

    for (const struct buffer *buffer = &gb->first; buffer != nullptr;
         buffer = buffer->next) {
        memcpy(dest, buffer->data, buffer->length);
        dest += buffer->length;
    }

    return dest;
}

WritableBuffer<void>
growing_buffer_dup(const GrowingBuffer *gb, struct pool *pool)
{
    size_t length;

    length = growing_buffer_size(gb);
    if (length == 0)
        return nullptr;

    void *dest = p_malloc(pool, length);
    growing_buffer_copy(dest, gb);

    return { dest, length };
}
