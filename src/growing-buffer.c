/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "growing-buffer.h"

#include <assert.h>
#include <string.h>

struct buffer {
    struct buffer *next;
    size_t length, position;
    char data[sizeof(size_t)];
};

struct growing_buffer {
    pool_t pool;
    struct istream stream;
    size_t size;
    struct buffer *current, first;
};

growing_buffer_t attr_malloc
growing_buffer_new(pool_t pool, size_t initial_size)
{
    growing_buffer_t gb = p_malloc(pool, sizeof(*gb) - sizeof(gb->first.data) + initial_size);

    gb->pool = pool;
    gb->size = initial_size;
    gb->current = &gb->first;
    gb->first.next = NULL;
    gb->first.length = 0;

    return gb;
}

void *
growing_buffer_write(growing_buffer_t gb, size_t length)
{
    struct buffer *buffer = gb->current;
    void *ret;

    assert(gb->size > 0);

    if (buffer->length + length > gb->size) {
        size_t grow = gb->size;
        if (grow < length)
            grow = length;
        buffer = p_malloc(gb->pool, sizeof(*buffer) - sizeof(buffer->data) + grow);
        buffer->next = NULL;
        buffer->length = 0;
        gb->current->next = buffer;
        gb->current = buffer;
    }

    assert(buffer->length + length <= gb->size);

    ret = buffer->data + buffer->length;
    buffer->length += length;

    return ret;
}

void
growing_buffer_write_buffer(growing_buffer_t gb, const void *p, size_t length)
{
    memcpy(growing_buffer_write(gb, length), p, length);
}

void
growing_buffer_write_string(growing_buffer_t gb, const char *p)
{
    growing_buffer_write_buffer(gb, p, strlen(p));
}


static inline growing_buffer_t
istream_to_gb(istream_t istream)
{
    return (growing_buffer_t)(((char*)istream) - offsetof(struct growing_buffer, stream));
}

static void
istream_gb_read(istream_t istream)
{
    growing_buffer_t gb = istream_to_gb(istream);
    size_t nbytes;

    assert(gb->pool == istream->pool);
    assert(gb->size == 0);
    assert(gb->current != NULL);
    assert(gb->current->position <= gb->current->length);

    while (gb->current->position == gb->current->length) {
        gb->current = gb->current->next;
        if (gb->current == NULL) {
            istream_invoke_eof(istream);
            return;
        }

        gb->current->position = 0;
    }

    nbytes = istream_invoke_data(istream, gb->current->data + gb->current->position,
                                 gb->current->length - gb->current->position);
    assert(nbytes <= gb->current->length - gb->current->position);

    gb->current->position += nbytes;

    while (gb->current->position == gb->current->length) {
        gb->current = gb->current->next;
        if (gb->current == NULL) {
            istream_invoke_eof(istream);
            return;
        }

        gb->current->position = 0;
    }
}

static void
istream_gb_close(istream_t istream)
{
    growing_buffer_t gb = istream_to_gb(istream);

    assert(gb->size == 0);

    gb->current = NULL;

    istream_invoke_free(istream);
}

static const struct istream istream_gb = {
    .read = istream_gb_read,
    .close = istream_gb_close,
};

istream_t
growing_buffer_istream(growing_buffer_t gb)
{
    assert(gb->size > 0);

    gb->size = 0; /* "read mode" marker for assertions */
    gb->first.position = 0;
    gb->current = &gb->first;
    gb->stream = istream_gb;
    gb->stream.pool = gb->pool;

    return &gb->stream;
}
