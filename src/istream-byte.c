/*
 * This istream filter passes one byte at a time.  This is useful for
 * testing and debugging istream handler implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <assert.h>
#include <string.h>

struct istream_byte {
    struct istream output;
    istream_t input;
};


/*
 * istream handler
 *
 */

static size_t
byte_source_data(const void *data, size_t length, void *ctx)
{
    struct istream_byte *byte = ctx;

    (void)length;

    return istream_invoke_data(&byte->output, data, 1);
}

static ssize_t
byte_source_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_byte *byte = ctx;

    (void)max_length;

    return istream_invoke_direct(&byte->output, type, fd, 1);
}

static void
byte_source_eof(void *ctx)
{
    struct istream_byte *byte = ctx;

    assert(byte->input != NULL);

    istream_clear_unref_handler(&byte->input);

    istream_invoke_eof(&byte->output);
}

static void
byte_source_abort(void *ctx)
{
    struct istream_byte *byte = ctx;

    assert(byte->input != NULL);

    istream_clear_unref(&byte->input);

    istream_invoke_abort(&byte->output);
}

static const struct istream_handler byte_input_handler = {
    .data = byte_source_data,
    .direct = byte_source_direct,
    .eof = byte_source_eof,
    .abort = byte_source_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_byte *
istream_to_byte(istream_t istream)
{
    return (struct istream_byte *)(((char*)istream) - offsetof(struct istream_byte, output));
}

static void
istream_byte_read(istream_t istream)
{
    struct istream_byte *byte = istream_to_byte(istream);

    istream_handler_set_direct(byte->input, byte->output.handler_direct);

    istream_read(byte->input);
}

static void
istream_byte_close(istream_t istream)
{
    struct istream_byte *byte = istream_to_byte(istream);

    assert(byte->input != NULL);

    istream_free_unref(&byte->input);
}

static const struct istream istream_byte = {
    .read = istream_byte_read,
    .close = istream_byte_close,
};


/*
 * constructor
 *
 */

istream_t
istream_byte_new(pool_t pool, istream_t input)
{
    struct istream_byte *byte = p_malloc(pool, sizeof(*byte));

    assert(input != NULL);
    assert(!istream_has_handler(input));

    byte->output = istream_byte;
    byte->output.pool = pool;

    istream_assign_ref_handler(&byte->input, input,
                               &byte_input_handler, byte,
                               0);

    return istream_struct_cast(&byte->output);
}
