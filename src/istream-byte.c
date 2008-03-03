/*
 * This istream filter passes one byte at a time.  This is useful for
 * testing and debugging istream handler implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>

struct istream_byte {
    struct istream output;
    istream_t input;
};


/*
 * istream handler
 *
 */

static size_t
byte_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_byte *byte = ctx;

    (void)length;

    return istream_invoke_data(&byte->output, data, 1);
}

static ssize_t
byte_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_byte *byte = ctx;

    (void)max_length;

    return istream_invoke_direct(&byte->output, type, fd, 1);
}

static void
byte_input_eof(void *ctx)
{
    struct istream_byte *byte = ctx;

    byte->input = NULL;
    istream_deinit_eof(&byte->output);
}

static void
byte_input_abort(void *ctx)
{
    struct istream_byte *byte = ctx;

    byte->input = NULL;
    istream_deinit_abort(&byte->output);
}

static const struct istream_handler byte_input_handler = {
    .data = byte_input_data,
    .direct = byte_input_direct,
    .eof = byte_input_eof,
    .abort = byte_input_abort,
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

    istream_free_handler(&byte->input);
    istream_deinit_abort(&byte->output);
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
    struct istream_byte *byte = istream_new_macro(pool, byte);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&byte->input, input,
                           &byte_input_handler, byte,
                           0);

    return istream_struct_cast(&byte->output);
}
