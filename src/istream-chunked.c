/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-buffer.h"
#include "format.h"

#include <assert.h>
#include <string.h>

struct istream_chunked {
    struct istream output;
    istream_t input;
    fifo_buffer_t buffer;
    size_t missing_from_current_chunk;
};


static int
chunked_is_closed(const struct istream_chunked *chunked)
{
    return chunked->buffer == NULL;
}

static void
chunked_eof_detected(struct istream_chunked *chunked)
{
    assert(chunked->input == NULL);
    assert(chunked->buffer != NULL);
    assert(fifo_buffer_empty(chunked->buffer));

    chunked->buffer = NULL;

    istream_deinit_eof(&chunked->output);
}

static void
chunked_try_write(struct istream_chunked *chunked)
{
    size_t rest;

    assert(chunked->buffer != NULL);

    rest = istream_buffer_consume(&chunked->output, chunked->buffer);
    if (rest == 0 && chunked->input == NULL)
        chunked_eof_detected(chunked);
}

static size_t
chunked_start_chunk(struct istream_chunked *chunked, size_t length, char *dest)
{
    assert(chunked->missing_from_current_chunk == 0);

    if (length > 0x1000)
        /* maximum chunk size is 4kB for now */
        length = 0x1000;

    chunked->missing_from_current_chunk = length;

    format_uint16_hex_fixed(dest, (uint16_t)length);
    dest[4] = '\r';
    dest[5] = '\n';

    return 6;
}

static void
chunked_start_chunk2(struct istream_chunked *chunked, size_t length)
{
    char *dest;
    size_t max_length, header_length;

    dest = fifo_buffer_write(chunked->buffer, &max_length);
    assert(dest != NULL);
    assert(max_length > 6);

    header_length = chunked_start_chunk(chunked, length, dest);
    assert(header_length <= max_length);

    fifo_buffer_append(chunked->buffer, header_length);
}


/*
 * istream handler
 *
 */

static size_t
chunked_source_data(const void *data, size_t length, void *ctx)
{
    struct istream_chunked *chunked = ctx;
    char *dest;
    size_t max_length, dest_length;

    assert(chunked->input != NULL);

    dest = fifo_buffer_write(chunked->buffer, &max_length);
    if (dest == NULL || max_length < 4 + 2 + 1 + 2 + 5) {
        /* the buffer is full - try to flush it */
        chunked_try_write(chunked);

        if (chunked->input == NULL)
            return 0;

        dest = fifo_buffer_write(chunked->buffer, &max_length);
        if (dest == NULL || max_length < 4 + 2 + 1 + 2 + 5)
            return 0;
    }


    if (chunked->missing_from_current_chunk == 0)
        dest_length = chunked_start_chunk(chunked, length, dest);
    else
        dest_length = 0;

    if (length > chunked->missing_from_current_chunk)
        length = chunked->missing_from_current_chunk;

    if (length > max_length - 4 - 2 - 2 - 5)
        length = max_length - 4 - 2 - 2 - 5;

    memcpy(dest + dest_length, data, length);
    dest_length += length;

    chunked->missing_from_current_chunk -= length;
    if (chunked->missing_from_current_chunk == 0) {
        dest[dest_length] = '\r';
        dest[dest_length + 1] = '\n';
        dest_length += 2;
    }

    fifo_buffer_append(chunked->buffer, dest_length);

    pool_ref(chunked->output.pool);

    chunked_try_write(chunked);
    if (chunked_is_closed(chunked))
        length = 0;

    pool_unref(chunked->output.pool);

    return length;
}

static void
chunked_source_eof(void *ctx)
{
    struct istream_chunked *chunked = ctx;
    char *dest;
    size_t max_length;

    assert(chunked->input != NULL);
    assert(chunked->buffer != NULL);
    assert(chunked->missing_from_current_chunk == 0);

    chunked->input = NULL;

    dest = fifo_buffer_write(chunked->buffer, &max_length);
    assert(dest != NULL && max_length >= 5); /* XXX */

    memcpy(dest, "0\r\n\r\n", 5);
    fifo_buffer_append(chunked->buffer, 5);

    chunked_try_write(chunked);
}

static const struct istream_handler chunked_source_handler = {
    .data = chunked_source_data,
    .eof = chunked_source_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_chunked *
istream_to_chunked(istream_t istream)
{
    return (struct istream_chunked *)(((char*)istream) - offsetof(struct istream_chunked, output));
}

static void
istream_chunked_read(istream_t istream)
{
    struct istream_chunked *chunked = istream_to_chunked(istream);

    if (chunked->input == NULL)
        chunked_try_write(chunked);
    else {
        if (chunked->missing_from_current_chunk == 0) {
            off_t available = istream_available(chunked->input, 1);
            if (available != (off_t)-1 && available > 0)
                chunked_start_chunk2(chunked, available);
        }

        istream_read(chunked->input);
    }
}

static void
istream_chunked_close(istream_t istream)
{
    struct istream_chunked *chunked = istream_to_chunked(istream);

    chunked->buffer = NULL;

    if (chunked->input != NULL)
        istream_free_handler(&chunked->input);

    istream_deinit_abort(&chunked->output);
}

static const struct istream istream_chunked = {
    .read = istream_chunked_read,
    .close = istream_chunked_close,
};


/*
 * constructor
 *
 */

istream_t
istream_chunked_new(pool_t pool, istream_t input)
{
    struct istream_chunked *chunked = istream_new_macro(pool, chunked);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    chunked->buffer = fifo_buffer_new(pool, 4096);
    chunked->missing_from_current_chunk = 0;

    istream_assign_handler(&chunked->input, input,
                           &chunked_source_handler, chunked,
                           0);

    return istream_struct_cast(&chunked->output);
}
