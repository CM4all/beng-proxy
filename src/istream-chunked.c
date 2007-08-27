/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "fifo-buffer.h"

#include <assert.h>
#include <string.h>

struct istream_chunked {
    struct istream output;
    istream_t input;
    fifo_buffer_t buffer;
};

static void
chunked_eof_detected(struct istream_chunked *chunked)
{
    assert(chunked->input == NULL);
    assert(chunked->buffer != NULL);
    assert(fifo_buffer_empty(chunked->buffer));

    chunked->buffer = NULL;

    pool_ref(chunked->output.pool);
    istream_invoke_eof(&chunked->output);
    istream_close(&chunked->output);
    pool_unref(chunked->output.pool);
}

static void
chunked_try_write(struct istream_chunked *chunked)
{
    const char *data;
    size_t length, nbytes;

    assert(chunked->buffer != NULL);

    data = fifo_buffer_read(chunked->buffer, &length);
    if (data == NULL)
        return;

    nbytes = istream_invoke_data(&chunked->output, data, length);
    assert(nbytes <= length);

    if (nbytes > 0) {
        fifo_buffer_consume(chunked->buffer, nbytes);
        if (nbytes == length && chunked->input == NULL)
            chunked_eof_detected(chunked);
    }
}


static const char hex_digits[] = "0123456789abcdef";

static size_t
chunked_source_data(const void *data, size_t length, void *ctx)
{
    struct istream_chunked *chunked = ctx;
    char *dest;
    size_t max_length;

    assert(chunked->input != NULL);

    dest = fifo_buffer_write(chunked->buffer, &max_length);
    if (dest == NULL)
        return 0;

    if (max_length < 4 + 2 + 1 + 2 + 5)
        return 0;

    if (length > max_length - 4 - 2 - 2 - 5)
        length = max_length - 4 - 2 - 2 - 5;

    dest[0] = hex_digits[(length >> 12) & 0xf];
    dest[1] = hex_digits[(length >> 8) & 0xf];
    dest[2] = hex_digits[(length >> 4) & 0xf];
    dest[3] = hex_digits[length & 0xf];
    dest[4] = '\r';
    dest[5] = '\n';
    memcpy(dest + 4 + 2, data, length);
    dest[4 + 2 + length] = '\r';
    dest[4 + 2 + length + 1] = '\n';

    fifo_buffer_append(chunked->buffer, 4 + 2 + length + 2);

    chunked_try_write(chunked);

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

    pool_unref(chunked->input->pool);
    chunked->input = NULL;

    dest = fifo_buffer_write(chunked->buffer, &max_length);
    assert(dest != NULL && max_length >= 5); /* XXX */

    memcpy(dest, "0\r\n\r\n", 5);
    fifo_buffer_append(chunked->buffer, 5);

    chunked_try_write(chunked);
}

static void
chunked_source_free(void *ctx)
{
    struct istream_chunked *chunked = ctx;

    if (chunked->input != NULL) {
        pool_unref(chunked->input->pool);
        chunked->input = NULL;
        chunked->buffer = NULL;

        istream_close(&chunked->output);
    }
}

static const struct istream_handler chunked_source_handler = {
    .data = chunked_source_data,
    .eof = chunked_source_eof,
    .free = chunked_source_free,
};


static inline struct istream_chunked *
istream_to_chunked(istream_t istream)
{
    return (struct istream_chunked *)(((char*)istream) - offsetof(struct istream_chunked, output));
}

static void
istream_chunked_read(istream_t istream)
{
    struct istream_chunked *chunked = istream_to_chunked(istream);

    chunked_try_write(chunked);
}

static void
istream_chunked_close(istream_t istream)
{
    struct istream_chunked *chunked = istream_to_chunked(istream);

    if (chunked->input != NULL) {
        pool_t pool = chunked->input->pool;
        istream_free(&chunked->input);
        pool_unref(pool);
    }
    
    chunked->buffer = NULL;

    istream_invoke_free(&chunked->output);
}

static const struct istream istream_chunked = {
    .read = istream_chunked_read,
    .close = istream_chunked_close,
};

istream_t
istream_chunked_new(pool_t pool, istream_t input)
{
    struct istream_chunked *chunked = p_malloc(pool, sizeof(*chunked));

    assert(input != NULL);
    assert(input->handler == NULL);

    chunked->output = istream_chunked;
    chunked->output.pool = pool;
    chunked->buffer = fifo_buffer_new(pool, 4096);
    chunked->input = input;

    input->handler = &chunked_source_handler;
    input->handler_ctx = chunked;
    pool_ref(input->pool);

    return &chunked->output;
}
