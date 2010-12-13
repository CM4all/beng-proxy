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

    char buffer[7];
    size_t buffer_sent;

    size_t missing_from_current_chunk;
};

static inline int
chunked_buffer_empty(const struct istream_chunked *chunked)
{
    assert(chunked->buffer_sent <= sizeof(chunked->buffer));

    return chunked->buffer_sent == sizeof(chunked->buffer);
}

/** set the buffer length and return a pointer to the first byte */
static inline char *
chunked_buffer_set(struct istream_chunked *chunked, size_t length)
{
    assert(chunked_buffer_empty(chunked));
    assert(length <= sizeof(chunked->buffer));

    chunked->buffer_sent = sizeof(chunked->buffer) - length;
    return chunked->buffer + chunked->buffer_sent;
}

/** append data to the buffer */
static void
chunked_buffer_append(struct istream_chunked *chunked,
                      const void *data, size_t length)
{
    const void *old = chunked->buffer + chunked->buffer_sent;
    size_t old_length = sizeof(chunked->buffer) - chunked->buffer_sent;
    char *dest;

    assert(data != NULL);
    assert(length > 0);
    assert(length <= chunked->buffer_sent);

#ifndef NDEBUG
    /* simulate a buffer reset; if we don't do this, an assertion in
       chunked_buffer_set() fails (which is invalid for this special
       case) */
    chunked->buffer_sent = sizeof(chunked->buffer);
#endif

    dest = chunked_buffer_set(chunked, old_length + length);
    memmove(dest, old, old_length);
    dest += old_length;

    memcpy(dest, data, length);
}

static void
chunked_start_chunk(struct istream_chunked *chunked, size_t length)
{
    char *buffer;

    assert(length > 0);
    assert(chunked_buffer_empty(chunked));
    assert(chunked->missing_from_current_chunk == 0);

    if (length > 0x8000)
        /* maximum chunk size is 32kB for now */
        length = 0x8000;

    chunked->missing_from_current_chunk = length;

    buffer = chunked_buffer_set(chunked, 6);
    format_uint16_hex_fixed(buffer, (uint16_t)length);
    buffer[4] = '\r';
    buffer[5] = '\n';
}

/**
 * Returns true if the buffer is consumed.
 */
static bool
chunked_write_buffer(struct istream_chunked *chunked)
{
    size_t length, nbytes;

    length = sizeof(chunked->buffer) - chunked->buffer_sent;
    if (length == 0)
        return true;

    nbytes = istream_invoke_data(&chunked->output,
                                 chunked->buffer + chunked->buffer_sent,
                                 length);
    if (nbytes > 0)
        chunked->buffer_sent += nbytes;

    return nbytes == length;
}

static size_t
chunked_feed(struct istream_chunked *chunked, const char *data, size_t length)
{
    size_t total = 0, rest, nbytes;
    bool bret;

    assert(chunked->input != NULL);

    do {
        if (chunked_buffer_empty(chunked) &&
            chunked->missing_from_current_chunk == 0)
            chunked_start_chunk(chunked, length - total);

        bret = chunked_write_buffer(chunked);
        if (!bret)
            return chunked->input == NULL ? 0 : total;

        assert(chunked_buffer_empty(chunked));

        if (chunked->missing_from_current_chunk == 0) {
            /* we have just written the previous chunk trailer;
               re-start this loop to start a new chunk */
            nbytes = rest = 0;
            continue;
        }

        rest = length - total;
        if (rest > chunked->missing_from_current_chunk)
            rest = chunked->missing_from_current_chunk;

        nbytes = istream_invoke_data(&chunked->output, data + total, rest);
        if (nbytes == 0)
            return chunked->input == NULL ? 0 : total;

        total += nbytes;

        chunked->missing_from_current_chunk -= nbytes;
        if (chunked->missing_from_current_chunk == 0) {
            /* a chunk ends with "\r\n" */
            char *buffer = chunked_buffer_set(chunked, 2);
            buffer[0] = '\r';
            buffer[1] = '\n';
        }
    } while ((!chunked_buffer_empty(chunked) || total < length) &&
             nbytes == rest);

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
    bool bret;

    assert(chunked->input != NULL);
    assert(chunked->missing_from_current_chunk == 0);

    chunked->input = NULL;

    /* write EOF chunk (length 0) */

    chunked_buffer_append(chunked, "0\r\n\r\n", 5);

    /* flush the buffer */

    bret = chunked_write_buffer(chunked);
    if (bret)
        istream_deinit_eof(&chunked->output);
}

static void
chunked_input_abort(void *ctx)
{
    struct istream_chunked *chunked = ctx;

    assert(chunked->input != NULL);

    chunked->input = NULL;

    istream_deinit_abort(&chunked->output);
}

static const struct istream_handler chunked_input_handler = {
    .data = chunked_input_data,
    .eof = chunked_input_eof,
    .abort = chunked_input_abort,
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
    bool bret;

    bret = chunked_write_buffer(chunked);
    if (!bret)
        return;

    if (chunked->input == NULL) {
        istream_deinit_eof(&chunked->output);
        return;
    }

    assert(chunked->input != NULL);

    if (chunked_buffer_empty(chunked) &&
        chunked->missing_from_current_chunk == 0) {
        off_t available = istream_available(chunked->input, true);
        if (available > 0) {
            chunked_start_chunk(chunked, available);
            bret = chunked_write_buffer(chunked);
            if (!bret)
                return;
        }
    }

    istream_read(chunked->input);
}

static void
istream_chunked_close(istream_t istream)
{
    struct istream_chunked *chunked = istream_to_chunked(istream);

    if (chunked->input != NULL)
        istream_free_handler(&chunked->input);

    istream_deinit(&chunked->output);
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
