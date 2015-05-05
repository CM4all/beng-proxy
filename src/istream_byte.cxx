/*
 * This istream filter passes one byte at a time.  This is useful for
 * testing and debugging istream handler implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_byte.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"

#include <assert.h>

struct istream_byte {
    struct istream output;
    struct istream *input;
};


/*
 * istream handler
 *
 */

static size_t
byte_input_data(const void *data, gcc_unused size_t length, void *ctx)
{
    auto *byte = (struct istream_byte *)ctx;

    return istream_invoke_data(&byte->output, data, 1);
}

static ssize_t
byte_input_direct(enum istream_direct type, int fd,
                  gcc_unused size_t max_length, void *ctx)
{
    auto *byte = (struct istream_byte *)ctx;

    return istream_invoke_direct(&byte->output, type, fd, 1);
}

static const struct istream_handler byte_input_handler = {
    .data = byte_input_data,
    .direct = byte_input_direct,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_byte *
istream_to_byte(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_byte::output);
}

static void
istream_byte_read(struct istream *istream)
{
    struct istream_byte *byte = istream_to_byte(istream);

    istream_handler_set_direct(byte->input, byte->output.handler_direct);

    istream_read(byte->input);
}

static void
istream_byte_close(struct istream *istream)
{
    struct istream_byte *byte = istream_to_byte(istream);

    assert(byte->input != nullptr);

    istream_close_handler(byte->input);
    istream_deinit(&byte->output);
}

static const struct istream_class istream_byte = {
    .read = istream_byte_read,
    .close = istream_byte_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_byte_new(struct pool *pool, struct istream *input)
{
    auto *byte = istream_new_macro(pool, byte);

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    istream_assign_handler(&byte->input, input,
                           &byte_input_handler, byte,
                           0);

    return istream_struct_cast(&byte->output);
}
