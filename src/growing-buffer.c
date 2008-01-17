/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "growing-buffer.h"
#include "valgrind.h"

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
    struct buffer *current, *tail, first;
};

growing_buffer_t attr_malloc
growing_buffer_new(pool_t pool, size_t initial_size)
{
    growing_buffer_t gb = p_malloc(pool, sizeof(*gb) - sizeof(gb->first.data) + initial_size);

    gb->pool = pool;
    gb->size = initial_size;
    gb->current = &gb->first;
    gb->tail = &gb->first;
    gb->first.next = NULL;
    gb->first.length = 0;
    gb->first.position = 0;

    return gb;
}

void *
growing_buffer_write(growing_buffer_t gb, size_t length)
{
    struct buffer *buffer = gb->tail;
    void *ret;

    assert(gb->size > 0);

    if (buffer->length + length > gb->size) {
        if (gb->size < length)
            gb->size = length; /* XXX round up? */
        buffer = p_malloc(gb->pool, sizeof(*buffer) - sizeof(buffer->data) + gb->size);
        buffer->next = NULL;
        buffer->length = 0;
        buffer->position = 0;
        gb->tail->next = buffer;
        gb->tail = buffer;
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

const void *
growing_buffer_read(growing_buffer_t gb, size_t *length_r)
{
    assert(gb->current != NULL);

    while (gb->current->position == gb->current->length) {
        assert(gb->current->position <= gb->current->length);
        if (gb->current->next == NULL)
            return NULL;
        gb->current = gb->current->next;
        assert(gb->current->position == 0);
    }

    assert(gb->current->position < gb->current->length);

    *length_r = gb->current->length - gb->current->position;
    return gb->current->data + gb->current->position;
}

void
growing_buffer_consume(growing_buffer_t gb, size_t length)
{
    assert(gb->current != NULL);
    assert(gb->current->position <= gb->current->length);

    while (length > 0) {
        assert(gb->current != NULL);

        if (gb->current->position + length < gb->current->length) {
            gb->current->position += length;
            length = 0;
        } else {
            length -= gb->current->length - gb->current->position;
            gb->current->position = gb->current->length;

            if (length > 0) {
                assert(gb->current->next != NULL);
                gb->current = gb->current->next;
            }
        }
    }

    if (gb->current->next == NULL &&
        gb->current->position == gb->current->length) {
        /* allow buffer recycling - we're on the last buffer, and
           someone might still be writing to it.  if we clear it here,
           the full buffer may be reused */
        gb->current->position = 0;
        gb->current->length = 0;
    }
}


static inline growing_buffer_t
istream_to_gb(istream_t istream)
{
    return (growing_buffer_t)(((char*)istream) - offsetof(struct growing_buffer, stream));
}

static off_t
istream_gb_available(istream_t istream, int partial attr_unused)
{
    growing_buffer_t gb = istream_to_gb(istream);
    struct buffer *buffer;
    off_t available = 0;

    assert(gb->pool == istream_pool(istream));
    assert(gb->size == 0);
    assert(gb->tail == NULL);
    assert(gb->current != NULL);
    assert(gb->current->position <= gb->current->length);

    for (buffer = gb->current; buffer != NULL; buffer = buffer->next)
        available += buffer->length - buffer->position;

    return available;
}

static void
istream_gb_read(istream_t istream)
{
    growing_buffer_t gb = istream_to_gb(istream);
    const void *data;
    size_t length, nbytes;

    assert(gb->pool == istream_pool(istream));
    assert(gb->size == 0);
    assert(gb->tail == NULL);
    assert(gb->current != NULL);
    assert(gb->current->position <= gb->current->length);

    data = growing_buffer_read(gb, &length);
    if (data == NULL) {
        gb->current = NULL;
        istream_invoke_eof(&gb->stream);
        return;
    }

    nbytes = istream_invoke_data(&gb->stream, data, length);
    assert(nbytes <= length);

    if (nbytes == 0)
        return;

    growing_buffer_consume(gb, nbytes);

    while (gb->current->position == gb->current->length) {
        gb->current = gb->current->next;
        if (gb->current == NULL) {
            istream_invoke_eof(&gb->stream);
            return;
        }

        assert(gb->current->position == 0);
    }
}

static void
istream_gb_close(istream_t istream)
{
    growing_buffer_t gb = istream_to_gb(istream);

    assert(gb->size == 0);
    assert(gb->tail == NULL);

    gb->current = NULL;

    istream_invoke_abort(&gb->stream);
}

static const struct istream istream_gb = {
    .available = istream_gb_available,
    .read = istream_gb_read,
    .close = istream_gb_close,
};

istream_t
growing_buffer_istream(growing_buffer_t gb)
{
    assert(gb->size > 0);

    gb->size = 0; /* "read mode" marker for assertions */
    assert(gb->first.position == 0);
    assert(gb->current == &gb->first);
    gb->tail = NULL;
    gb->stream = istream_gb;
    gb->stream.pool = gb->pool;

    return istream_struct_cast(&gb->stream);
}
