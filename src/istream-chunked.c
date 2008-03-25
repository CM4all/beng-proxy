/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "format.h"

#include <assert.h>
#include <string.h>

struct istream_chunked {
    struct istream output;
    istream_t input;

    char buffer[6];
    size_t buffer_sent;

    size_t missing_from_current_chunk;
};


static void
chunked_start_chunk(struct istream_chunked *chunked, size_t length)
{
    assert(length > 0);
    assert(chunked->buffer_sent == sizeof(chunked->buffer));
    assert(chunked->missing_from_current_chunk == 0);

    if (length > 0x8000)
        /* maximum chunk size is 32kB for now */
        length = 0x8000;

    chunked->missing_from_current_chunk = length;

    format_uint16_hex_fixed(chunked->buffer, (uint16_t)length);
    chunked->buffer[4] = '\r';
    chunked->buffer[5] = '\n';
    chunked->buffer_sent = 0;
}

static size_t
chunked_write_buffer(struct istream_chunked *chunked)
{
    size_t rest, nbytes;

    rest = sizeof(chunked->buffer) - chunked->buffer_sent;
    if (rest == 0)
        return 0;

    nbytes = istream_invoke_data(&chunked->output,
                                 chunked->buffer + chunked->buffer_sent,
                                 rest);
    if (nbytes == 0)
        return rest;

    chunked->buffer_sent += nbytes;
    return rest - nbytes;
}

static size_t
chunked_feed(struct istream_chunked *chunked, const char *data, size_t length)
{
    size_t total = 0, rest, nbytes;

    assert(chunked->input != NULL);

    do {
        if (chunked->missing_from_current_chunk == 0)
            chunked_start_chunk(chunked, length - total);

        rest = chunked_write_buffer(chunked);
        if (rest > 0)
            return chunked->input == NULL ? 0 : total;

        rest = length - total;
        if (rest > chunked->missing_from_current_chunk)
            rest = chunked->missing_from_current_chunk;

        nbytes = istream_invoke_data(&chunked->output, data + total, rest);
        if (nbytes == 0)
            return chunked->input == NULL ? 0 : total;

        chunked->missing_from_current_chunk -= nbytes;
        total += nbytes;
    } while (total < length && nbytes == rest);

    return total;
}


/*
 * istream handler
 *
 */

static size_t
chunked_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_chunked *chunked = ctx;
    size_t nbytes;

    pool_ref(chunked->output.pool);
    nbytes = chunked_feed(chunked, (const char*)data, length);
    pool_unref(chunked->output.pool);

    return nbytes;
}

static void
chunked_input_eof(void *ctx)
{
    struct istream_chunked *chunked = ctx;

    assert(chunked->input != NULL);
    assert(chunked->missing_from_current_chunk == 0);

    chunked->input = NULL;

    /* write EOF chunk (length 0) */

    memcpy(chunked->buffer + sizeof(chunked->buffer) - 5, "0\r\n\r\n", 5);
    chunked->buffer_sent = sizeof(chunked->buffer) - 5;

    /* flush the buffer */

    chunked_write_buffer(chunked);
}

static const struct istream_handler chunked_input_handler = {
    .data = chunked_input_data,
    .eof = chunked_input_eof,
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
    size_t rest;

    rest = chunked_write_buffer(chunked);
    if (rest > 0)
        return;

    if (chunked->input == NULL) {
        istream_deinit_eof(&chunked->output);
        return;
    }

    assert(chunked->input != NULL);

    if (chunked->missing_from_current_chunk == 0) {
        off_t available = istream_available(chunked->input, 1);
        if (available != (off_t)-1 && available > 0)
            chunked_start_chunk(chunked, available);
    }

    istream_read(chunked->input);
}

static void
istream_chunked_close(istream_t istream)
{
    struct istream_chunked *chunked = istream_to_chunked(istream);

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

    chunked->buffer_sent = sizeof(chunked->buffer);
    chunked->missing_from_current_chunk = 0;

    istream_assign_handler(&chunked->input, input,
                           &chunked_input_handler, chunked,
                           0);

    return istream_struct_cast(&chunked->output);
}
