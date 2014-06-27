/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "growing_buffer.hxx"
#include "pool.h"

#include <assert.h>
#include <string.h>

struct buffer {
    struct buffer *next;
    size_t length;
    char data[sizeof(size_t)];
};

struct growing_buffer {
    struct pool *pool;

#ifndef NDEBUG
    size_t initial_size;
#endif

    size_t size;
    struct buffer *current, *tail, first;
};

struct growing_buffer *gcc_malloc
growing_buffer_new(struct pool *pool, size_t initial_size)
{
    struct growing_buffer *gb = (struct growing_buffer *)
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
growing_buffer_append_buffer(struct growing_buffer *gb, struct buffer *buffer)
{
    assert(gb != nullptr);
    assert(buffer != nullptr);
    assert(buffer->next == nullptr);

    gb->tail->next = buffer;
    gb->tail = buffer;
}

void *
growing_buffer_write(struct growing_buffer *gb, size_t length)
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
growing_buffer_write_buffer(struct growing_buffer *gb, const void *p, size_t length)
{
    memcpy(growing_buffer_write(gb, length), p, length);
}

void
growing_buffer_write_string(struct growing_buffer *gb, const char *p)
{
    growing_buffer_write_buffer(gb, p, strlen(p));
}

void
growing_buffer_cat(struct growing_buffer *dest, struct growing_buffer *src)
{
    dest->tail->next = &src->first;
    dest->tail = src->tail;
    dest->size = src->size;
}

size_t
growing_buffer_size(const struct growing_buffer *gb)
{
    size_t size = 0;

    for (const struct buffer *buffer = &gb->first;
         buffer != nullptr; buffer = buffer->next)
        size += buffer->length;

    return size;
}

growing_buffer_reader::growing_buffer_reader(const struct growing_buffer &gb)
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
growing_buffer_reader::Update()
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
growing_buffer_reader::IsEOF() const
{
    assert(buffer != nullptr);
    assert(position <= buffer->length);

    return position == buffer->length;
}

size_t
growing_buffer_reader::Available() const
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

const void *
growing_buffer_reader::Read(size_t *length_r) const
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

    *length_r = b->length - position;
    return b->data + position;
}

void
growing_buffer_reader::Consume(size_t length)
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
growing_buffer_reader::Skip(size_t length)
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
growing_buffer_copy(void *dest0, const struct growing_buffer *gb)
{
    unsigned char *dest = (unsigned char *)dest0;

    for (const struct buffer *buffer = &gb->first; buffer != nullptr;
         buffer = buffer->next) {
        memcpy(dest, buffer->data, buffer->length);
        dest += buffer->length;
    }

    return dest;
}

void *
growing_buffer_dup(const struct growing_buffer *gb, struct pool *pool,
                   size_t *length_r)
{
    size_t length;

    length = growing_buffer_size(gb);
    *length_r = length;
    if (length == 0)
        return nullptr;

    void *dest = p_malloc(pool, length);
    growing_buffer_copy(dest, gb);

    return dest;
}

void *
growing_buffer_dup2(const struct growing_buffer *a,
                    const struct growing_buffer *b,
                    struct pool *pool, size_t *length_r)
{
    size_t length;

    length = growing_buffer_size(a) + growing_buffer_size(b);
    *length_r = length;
    if (length == 0)
        return nullptr;

    void *dest = p_malloc(pool, length);
    growing_buffer_copy(growing_buffer_copy(dest, a), b);

    return dest;
}
