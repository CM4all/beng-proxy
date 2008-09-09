/*
 * This istream filter passes no more than four bytes at a time.  This
 * is useful for testing and debugging istream handler
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>

struct istream_four {
    struct istream output;
    istream_t input;
};


/*
 * istream handler
 *
 */

static size_t
four_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_four *four = ctx;

    if (length > 4)
        length = 4;

    return istream_invoke_data(&four->output, data, length);
}

static ssize_t
four_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_four *four = ctx;

    if (max_length > 4)
        max_length = 4;

    return istream_invoke_direct(&four->output, type, fd, max_length);
}

static const struct istream_handler four_input_handler = {
    .data = four_input_data,
    .direct = four_input_direct,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_four *
istream_to_four(istream_t istream)
{
    return (struct istream_four *)(((char*)istream) - offsetof(struct istream_four, output));
}

static void
istream_four_read(istream_t istream)
{
    struct istream_four *four = istream_to_four(istream);

    istream_handler_set_direct(four->input, four->output.handler_direct);

    istream_read(four->input);
}

static void
istream_four_close(istream_t istream)
{
    struct istream_four *four = istream_to_four(istream);

    assert(four->input != NULL);

    istream_close_handler(four->input);
    istream_deinit_abort(&four->output);
}

static const struct istream istream_four = {
    .read = istream_four_read,
    .close = istream_four_close,
};


/*
 * constructor
 *
 */

istream_t
istream_four_new(pool_t pool, istream_t input)
{
    struct istream_four *four = istream_new_macro(pool, four);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&four->input, input,
                           &four_input_handler, four,
                           0);

    return istream_struct_cast(&four->output);
}
